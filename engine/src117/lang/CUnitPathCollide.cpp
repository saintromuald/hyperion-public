// CUnitPathCollide.cpp -- the unit-to-unit collision queries the path state
// machine calls: who is standing where this unit wants to go, and where the
// nearest free spot is. Bodies are in oracle VA order.
//
// A callee defined in this file can be spliced into a caller here by /Ob2, while the
// oracle, built with WPO off, cannot splice across a TU boundary: a callee left in another
// TU reproduces that for free, and __declspec(noinline) is the lever when one must live
// here.

#include "oc_data.h"  // Hyperion: OC_VA
#include <stdlib.h>                 // abs() -- an MSVC intrinsic under /Oi

#include "../h/CUnitPathCollide.h"
#include "../h/CUnit.h"
#include "../h/CSprite.h"
#include "../h/CFlingy.h"
#include "../h/CUnitCombat.h"
#include "../h/CUnitTSpells.h"
#include "../h/SAI_Banker.h"
#include "../h/CTarget.h"
#include "../h/CUnitBuild.h"
#include "../h/CUnitFind.h"
#include "../h/CUnitPathUtil.h"
#include "../h/rect16.h"      // ours: the gUnitBoundBox row type
#include "../h/sai.h"
#include "../h/sai_PathFindUtil.h"
#include "../h/sai_influence.h"
#include "../h/sai_path.h"      // ours: the gUnitBoundBox row type
#include "../h/sai_PathCreate.h"  // ours: ep_glob_t, epGlob's pContours
#include "../h/CUnitBounds.h"     // pos_sort_ent_t
#include "../gen/globals117.h"

// The globals this TU reaches, dual-spelled: one #ifdef per name, never one over the whole
// block, since the generated set grows over time and a single guard would go false and
// undeclare every name under it.

#ifdef OC_DLL_MODE
#ifndef DIRECTIONS
#define DIRECTIONS (reinterpret_cast<LONG *>(OC_VA(0x5026B0u)))
#endif
#ifndef gUnitBoundBox
#define gUnitBoundBox (reinterpret_cast<rect16_t *>(OC_VA(0x522C38u)))
#endif
#ifndef gwMapPixWidth
#define gwMapPixWidth (*reinterpret_cast<WORD *>(OC_VA(0x543230u)))
#endif
#ifndef gwMapPixHeight
#define gwMapPixHeight (*reinterpret_cast<WORD *>(OC_VA(0x543234u)))
#endif
#ifndef sgwStagedX_g
#define sgwStagedX_g (*reinterpret_cast<WORD *>(OC_VA(0x543280u)))
#endif
#ifndef sgwStagedY_g
#define sgwStagedY_g (*reinterpret_cast<WORD *>(OC_VA(0x543284u)))
#endif
#else
extern LONG     DIRECTIONS[512];     // 0x5026B0, registered; flat {x,y} pairs
extern rect16_t gUnitBoundBox[228] OC_UNALIGNED;  // 0x522C38, registered
extern upos_t   gwMapPixWidth;       // 0x543230, registered
extern upos_t   gwMapPixHeight;      // 0x543234, registered
extern upos_t   sgwStagedX_g;        // 0x543280, registered
extern upos_t   sgwStagedY_g;        // 0x543284, registered
#endif

#ifndef OC_DLL_MODE
extern pos_sort_ent_t g_pos_sort_x[POOL_FINDER_STORE];  // 0x5C99B8, registered untyped
#endif
#ifdef OC_DLL_MODE
#define g_pos_sort_x (reinterpret_cast<pos_sort_ent_t *>(OC_VA(0x5C99B8u)))
#endif
#ifndef OC_DLL_MODE
extern pos_sort_ent_t g_pos_sort_y[POOL_FINDER_STORE];  // 0x5D03F8, registered untyped
#endif
#ifdef OC_DLL_MODE
#define g_pos_sort_y (reinterpret_cast<pos_sort_ent_t *>(OC_VA(0x5D03F8u)))
#endif

// CUnit +0x13C..+0x148, the unit's indices into the two sort arrays. Same
// spelling as CUnitBounds.cpp and collide.cpp.
#define UNIT_BOUND_LEFT(u)   (*reinterpret_cast<int *>(reinterpret_cast<BYTE *>(u) + offsetof(CUnit, _unmapped_138) + 0x04))
#define UNIT_BOUND_RIGHT(u)  (*reinterpret_cast<int *>(reinterpret_cast<BYTE *>(u) + offsetof(CUnit, _unmapped_138) + 0x08))
#define UNIT_BOUND_TOP(u)    (*reinterpret_cast<int *>(reinterpret_cast<BYTE *>(u) + offsetof(CUnit, _unmapped_138) + 0x0C))
#define UNIT_BOUND_BOTTOM(u) (*reinterpret_cast<int *>(reinterpret_cast<BYTE *>(u) + offsetof(CUnit, _unmapped_138) + 0x10))

// The bodies, in oracle VA order.

// 0x42CA80, 264 B. Resolve this unit sitting on top of another one: find the
// nearest free spot, retarget the unit at it and take one step. A unit that is
// not movable, or already where it would be sent, is only flagged.
//
// The rect at +0x108 is the one sub_42DA50 reads back field by field. Clear it
// a field at a time: that is what merges into two register-sourced stores.
BOOL CUnit::CheckResolveCollision()
{
  if (!(dwFlags & 0x100000))
    return FALSE;

  mpoint_t  pt;

  pt.x = pSprite->wPosX;
  pt.y = pSprite->wPosY;

  if (!sub_42EAB0(pt))
    return FALSE;

  DWORD dwUnitFlags = dwFlags;

  if ((dwUnitFlags & 0x3000) || (bMoveFlags & 8))
  {
    dwFlags = dwUnitFlags | 0x40000;
    return FALSE;
  }

  FindFree(&pt);
  ClipToMap(&pt, wType);

  if (pt.x == PosS(pSprite->wPosX) && pt.y == pSprite->wPosY)
  {
    dwFlags |= 0x40000;
    return FALSE;
  }

  sai_path_kill(this);

  if (!AtMoveTarget() && !(bMoveFlags & 4))
    SetSubTarget(pt.x, pt.y);
  else
    SetMoveTarget_xy(pt.x, pt.y);

  PathOneStep(pt, pt);
  rect16_t *prcSearch = reinterpret_cast<rect16_t *>(&_unmapped_106[2]);

  prcSearch->left = 0;
  prcSearch->top = 0;
  prcSearch->right = 0;
  prcSearch->bottom = 0;
  return TRUE;
}

// A clipped line segment as this TU's staging buffer holds it: two packed
// endpoints, 8 B. The stride is what sub_42CB90's `sar ebx, 3h` counts.
struct clipseg_t {
  /*0x00*/ DWORD dwFrom;
  /*0x04*/ DWORD dwTo;
};

static_assert(sizeof(clipseg_t)            == 0x08, "clipseg_t size     sar ebx, 3h");
static_assert(offsetof(clipseg_t, dwFrom)  == 0x00, "clipseg_t::dwFrom  mov [ebx], edx");
static_assert(offsetof(clipseg_t, dwTo)    == 0x04, "clipseg_t::dwTo    mov [ebx+4h], ecx");

// 0x42CB90, 223 B. Clip a run of segments against the influence map in place
// and answer how many came back. With nArg3 set only a segment that survives
// whole is kept; otherwise the trimmed-off parts are kept, so one input can
// produce two outputs. The 256-segment staging buffer is copied back over the
// caller's array, which is both source and destination.
//
// The clip outputs are point16_t locals read through DWORD references; plain DWORD
// locals swap the compared values' registers and nothing is cross-jumped.
int sub_42CB90(intptr_t nArg0, int nArg1, intptr_t nArg2, int nArg3)
{
  clipseg_t aStage[256];
  clipseg_t *pOut = aStage;
  clipseg_t *pIn = reinterpret_cast<clipseg_t *>(nArg0);

  if (nArg1 > 0)
  {
    do
    {
      point16_t ptFrom = *reinterpret_cast<point16_t *>(&pIn->dwFrom);
      point16_t ptTo = *reinterpret_cast<point16_t *>(&pIn->dwTo);
      DWORD &dwFrom = *reinterpret_cast<DWORD *>(&ptFrom);
      DWORD &dwTo = *reinterpret_cast<DWORD *>(&ptTo);
      // Identical arms under nArg3 keep the rect in ECX and the counter in its slot.
      BOOL bClipped;
      if (nArg3)
        bClipped = LineClip(&ptFrom, &ptTo, reinterpret_cast<const rect16_t *>(nArg2));
      else
        bClipped = LineClip(&ptFrom, &ptTo, reinterpret_cast<const rect16_t *>(nArg2));

      if (nArg3)
      {
        if (bClipped)
        {
          pOut->dwFrom = dwFrom;
          pOut->dwTo = dwTo;
          pOut++;
        }
      }
      else if (!bClipped)
      {
        pOut->dwFrom = pIn->dwFrom;
        pOut->dwTo = pIn->dwTo;
        pOut++;
      }
      else if (dwFrom != pIn->dwFrom && dwTo != pIn->dwTo)
      {
        pOut[0].dwFrom = pIn->dwFrom;
        pOut[0].dwTo = dwFrom;
        pOut[1].dwFrom = dwTo;
        pOut[1].dwTo = pIn->dwTo;
        pOut += 2;
      }
      else if (dwFrom != pIn->dwFrom)
      {
        pOut->dwFrom = pIn->dwFrom;
        pOut->dwTo = dwFrom;
        pOut++;
      }
      else if (dwTo != pIn->dwTo)
      {
        pOut->dwFrom = dwTo;
        pOut->dwTo = pIn->dwTo;
        pOut++;
      }

      pIn++;
    } while (--nArg1);
  }

  clipseg_t *pDst = reinterpret_cast<clipseg_t *>(nArg0);
  int nKept = (int)(pOut - aStage);

  if (nKept > 0)
  {
    for (int i = 0; i < nKept; i++)
      pDst[i] = aStage[i];
  }

  return nKept;
}

// 0x42CC70, 359 B. Which side of the other unit this unit is nearest to, as
// 0/1/2/3: an edge the boxes do not cross, else the smallest edge gap.
// The rects split into scalars, one in the dead parameter slot; an int type
// index keeps the scaled addressing. nSide is set after the second gap, which
// puts the fourth compare's operands in si/di.
int CUnit::sub_42CC70(void *pArg0)
{
  CUnit *pOther = static_cast<CUnit *>(pArg0);
  mrect_t  rcOther;
  upos_t wOtherY = pOther->pSprite->wPosY;
  upos_t wOtherX = pOther->pSprite->wPosX;
  WORD wOtherType = pOther->wType;

  rcOther.top    = wOtherY - gUnitBoundBox[wOtherType].top;
  rcOther.bottom = gUnitBoundBox[wOtherType].bottom + wOtherY;
  rcOther.left   = wOtherX - gUnitBoundBox[wOtherType].left;
  rcOther.right  = gUnitBoundBox[wOtherType].right + wOtherX;

  mrect_t  rc;
  upos_t wPosY = pSprite->wPosY;
  upos_t wPosX = pSprite->wPosX;
  int nUnitType = wType;

  rc.top    = wPosY - gUnitBoundBox[nUnitType].top;
  rc.bottom = gUnitBoundBox[nUnitType].bottom + wPosY;
  rc.left   = wPosX - gUnitBoundBox[nUnitType].left;
  rc.right  = gUnitBoundBox[nUnitType].right + wPosX;

  if (rcOther.right < rc.left)
    return 3;

  if (rcOther.left > rc.right)
    return 1;

  if (rcOther.bottom < rc.top)
    return 0;

  if (rcOther.top > rc.bottom)
    return 2;

  int nRightGap = abs(rcOther.right - rc.left);
  int nTopGap = abs(rcOther.top - rc.bottom);
  int nSide = 1;
  int nBottomGap = abs(rcOther.bottom - rc.top);
  int nBest = abs(rcOther.left - rc.right);

  if (nRightGap < nBest)
  {
    nSide = 3;
    nBest = nRightGap;
  }

  if (nTopGap < nBest)
  {
    nSide = 2;
    nBest = nTopGap;
  }

  if (nBottomGap < nBest)
    nSide = 0;

  return nSide;
}

// 0x42CDE0, 116 B. Whether the heading to the other unit is far enough off
// that unit's own facing to count as head-on. The facing is quantized to four
// quarters first.
//
// The last quarter is the oracle's `cmp / sbb / and`: a plain else-if chain
// ending in `else bQuarter = 0`. Every compare is unsigned.
int CUnit::sub_42CDE0(void *pArg0)
{
  CUnit *pOther = static_cast<CUnit *>(pArg0);
  upos_t wOtherY = pOther->pSprite->wPosY;
  upos_t wOtherX = pOther->pSprite->wPosX;
  upos_t wPosY = pSprite->wPosY;
  upos_t wPosX = pSprite->wPosX;
  int nAngle = AngleBetween(PosS(wPosX), PosS(wPosY),
                            PosS(wOtherX), PosS(wOtherY));
  BYTE bFacing = pOther->bDirection;
  BYTE bQuarter;

  if (bFacing < 0x20)
    bQuarter = 0;
  else if (bFacing < 0x60)
    bQuarter = 0x40;
  else if (bFacing < 0xA0)
    bQuarter = 0x80;
  else if (bFacing < 0xE0)
    bQuarter = 0xC0;
  else
    bQuarter = 0;

  return get_angle(static_cast<BYTE>(nAngle), bQuarter) > 0x40;
}

// 0x42CE60, 139 B. The nearest position this unit can stand in, written
// through pPos. Free ground answers from the point itself; blocked ground
// answers from whichever unit is standing there. Both callees write through
// pPos, so the pointer is returned by this body, not forwarded from the call.
// wPosY is read before wPosX, as the oracle loads them.
mpoint_t *CUnit::FindFree(mpoint_t *pPos)
{
  upos_t wPosY = pSprite->wPosY;
  upos_t wPosX = pSprite->wPosX;

  if (!sub_42ED50(wType, wPosX, wPosY))
  {
    FindFree_FromPoint(pPos);
    return pPos;
  }

  mpoint_t  pt;

  pt.x = pSprite->wPosX;
  pt.y = pSprite->wPosY;

  intptr_t nBlocker = sub_42EAB0(pt);

  if (!nBlocker)
  {
    *pPos = pt;  // Hyperion: the point, not its DWORD
    return pPos;
  }

  sub_42DA50(0, 0);
  // Hyperion: FindFree_FromUnit keeps 1.17's point with the clip lines, which
  // widen in stage F
  point16_t ptFree = *pPos;
  FindFree_FromUnit(&ptFree, reinterpret_cast<void *>(nBlocker));
  *pPos = ptFree;
  return pPos;
}

// The query block GetClosestReachable fills. Only the start point at +0x18 is
// derived; the rest is the 0x16C the oracle's frame reserves around it.
// Hyperion: the block is path_search_t and ptStart its ptCur; on x64 the view
// follows path_search_t's layout, 0x18 and 0x150 bytes around ptStart in 1.17
//.
struct reachable_query_t {
  /*0x000*/ BYTE      _unmapped_000[offsetof(path_search_t, ptCur)];
  /*0x018*/ mpoint_t  ptStart;  // Hyperion: path_search_t's ptCur, of 4-byte positions on x64 (stage E)
  /*0x01C*/ BYTE      _unmapped_01C[sizeof(path_search_t) - offsetof(path_search_t, ptCur) - sizeof(mpoint_t)];
};

OC_X86_LAYOUT(sizeof(reachable_query_t) == 0x16C,
              "reachable_query_t size     the 0x18C frame less its 0x20 of scalars");
OC_X86_LAYOUT(offsetof(reachable_query_t, ptStart) == 0x18,
              "reachable_query_t::ptStart  mov word ptr [ebp-174h], ax with lea ecx, [ebp-18Ch]");
static_assert(sizeof(reachable_query_t) == sizeof(path_search_t), "reachable_query_t is path_search_t");

// 0x42CEF0, 935 B. The nearest standable point when this unit is boxed in: a
// reachable region walks toward the best neighbour's closest point, an
// unreachable one is pushed along a randomized heading. Sprite coordinates are
// read into WORD locals and cast to short at the use. The early exits jump to
// the reach test's return, so pPos's one return copy covers the y push.
mpoint_t *CUnit::FindFree_FromPoint(mpoint_t *pPos)
{
  pPos->x = 0;
  pPos->y = 0;
  upos_t wSeedY = pSprite->wPosY;
  upos_t wSeedX = pSprite->wPosX;
  if (IsInaccessibleRegion_xy(PosS(wSeedX), PosS(wSeedY))) {
    upos_t wPosY = pSprite->wPosY;
    upos_t wPosX = pSprite->wPosX;
    WORD *pPaths;
    int nPaths = GetRegionPaths(GetRegion_xy(PosS(wPosX),
                                             PosS(wPosY)), &pPaths);
    upos_t wY = pSprite->wPosY;
    upos_t wX = pSprite->wPosX;
    if (!nPaths) {
      int nCount = GetRegionNeighbors(GetRegion_xy(PosS(wX),
                                           PosS(wY)), &pPaths);
      GetRegionPosition(pPaths[net_randbetween(0, nCount - 1, 0x32)], pPos);
      int nPy = pPos->y;
      int nPx = pPos->x;
      upos_t wSprY = pSprite->wPosY;
      upos_t wSprX = pSprite->wPosX;
      int nY0 = nPy;
      int nX0 = nPx;
      int nStep = veclen(nX0, PosS(wSprX), nY0, PosS(wSprY));
      if (nStep > 0x40) {
        nStep = 0x40;
      }
      upos_t wAngY = pSprite->wPosY;
      upos_t wAngX = pSprite->wPosX;
      BYTE bDir = static_cast<BYTE>(AngleBetween(PosS(wAngX),
                                                 PosS(wAngY),
                                                 pPos->x, pPos->y));
      pPos->x = PosS(((DIRECTIONS[bDir * 2] * nStep) >> 8) + pSprite->wPosX);
      pPos->y = PosS(((DIRECTIONS[bDir * 2 + 1] * nStep) >> 8) + pSprite->wPosY);
    } else {
      int nBest = 0x8000;
      int nRegion = GetRegion_xy(PosS(wX), PosS(wY));
      for (int i = 0; i < nPaths; i++) {
        GetRegionPosition(pPaths[i], pPos);
        int nPy = pPos->y;
        int nPx = pPos->x;
        upos_t wSprY = pSprite->wPosY;
        upos_t wSprX = pSprite->wPosX;
        int nY0 = nPy;
        int nX0 = nPx;
        int nDist = veclen(nX0, PosS(wSprX), nY0, PosS(wSprY));
        if (nDist < nBest) {
          nRegion = pPaths[i];
          nBest = nDist;
        }
      }
      rect16_t rcBounds;
      GetRegionBounds(static_cast<WORD>(nRegion), &rcBounds);
      if (PosS(pSprite->wPosX) + 0x20 >= rcBounds.left
          && PosS(pSprite->wPosX) - 0x20 < rcBounds.right
          && PosS(pSprite->wPosY) + 0x20 >= rcBounds.top
          && PosS(pSprite->wPosY) - 0x20 < rcBounds.bottom) {
        reachable_query_t q;
        q.ptStart.x = pSprite->wPosX;
        q.ptStart.y = pSprite->wPosY;
        GetClosestReachable(reinterpret_cast<path_search_t *>(&q), nRegion);
        *pPos = q.ptStart;
        if (!(PosU(pPos->x) == pSprite->wPosX
              && pPos->y == PosS(pSprite->wPosY)))
          goto done;
        upos_t wAngY = pSprite->wPosY;
        upos_t wAngX = pSprite->wPosX;
        BYTE bDir = static_cast<BYTE>(AngleBetween(PosS(wAngX),
                                                   PosS(wAngY),
                                                   pPos->x, pPos->y));
        pPos->x = PosS(((DIRECTIONS[bDir * 2] << 6) >> 8) + pSprite->wPosX);
        pPos->y = PosS(((DIRECTIONS[bDir * 2 + 1] << 6) >> 8) + pSprite->wPosY);
      } else {
        upos_t wAngY = pSprite->wPosY;
        upos_t wAngX = pSprite->wPosX;
        BYTE bDir = static_cast<BYTE>(AngleBetween(PosS(wAngX),
                                                   PosS(wAngY),
                                                   pPos->x, pPos->y));
        pPos->x = PosS(((DIRECTIONS[bDir * 2] << 6) >> 8) + pSprite->wPosX);
        pPos->y = PosS(((DIRECTIONS[bDir * 2 + 1] << 6) >> 8) + pSprite->wPosY);
      }
    }
    goto done;
  }
  {  // Hyperion: a block of its own, as the gotos to done jump past it
    upos_t wPosY = pSprite->wPosY;
    upos_t wPosX = pSprite->wPosX;
    GetRegionPosition(GetRegion_xy(PosS(wPosX), PosS(wPosY)), pPos);
    upos_t wAngY = pSprite->wPosY;
    upos_t wAngX = pSprite->wPosX;
    BYTE bDir = static_cast<BYTE>(AngleBetween(PosS(wAngX),
                                               PosS(wAngY),
                                               pPos->x, pPos->y));
    bDir += static_cast<BYTE>(net_randbetween(-2, 2, 0x32) << 4);
    int nSpread = net_randbetween(2, 4, 0x32) << 11;
    upos_t wLenY = pSprite->wPosY;
    upos_t wLenX = pSprite->wPosX;
    int nLimit = veclen(PosS(wLenX), pPos->x,
                        PosS(wLenY), pPos->y) << 8;
    if (nSpread >= nLimit) {
      nSpread = nLimit - 1;
    }
    int nDx = DIRECTIONS[bDir * 2] * nSpread;
    int nDy = DIRECTIONS[bDir * 2 + 1] * nSpread >> 8;
    pPos->x = PosS((nDx >> 16) + pSprite->wPosX);
    pPos->y = PosS((nDy >> 8) + pSprite->wPosY);
  }
  if (UnitCanReachGroundTarget_xy(this, pPos->x, pPos->y)) {
done:
    return pPos;
  }
  pPos->x = pSprite->wPosX;
  pPos->y = pSprite->wPosY;
  return pPos;
}

// 0x42D2A0, 1235 B. Where this unit steps to get out of pOther's way: the
// nearest standable point on pOther's box edges, clipped against the units
// around it, else a random step along its heading or toward its move target,
// else where it stands.
#if !defined(OC_PAYLOAD) || defined(OC_WITH_HELD)
// clipseg_t read as two points.
struct clipline_t {
  /*0x00*/ point16_t ptFrom;
  /*0x04*/ point16_t ptTo;
};

static_assert(sizeof(clipline_t)            == 0x08, "clipline_t size");
static_assert(offsetof(clipline_t, ptFrom)  == 0x00, "clipline_t::ptFrom  word [ebx-6h]");
static_assert(offsetof(clipline_t, ptTo)    == 0x04, "clipline_t::ptTo    word [ebx-2h]");

// CUnit::AtMoveTarget inlined.
static __forceinline BOOL collide_at_move_target(CUnit *pUnit)
{
  CSprite *pSpr = pUnit->pSprite;

  if (pUnit->wMoveTargetX == pSpr->wPosX
      && pUnit->wMoveTargetY == PosU(pSpr->wPosY))
    return (pUnit->dwFlags & 0x80000) ? 2 : 1;

  return 0;
}

void CUnit::FindFree_FromUnit(point16_t *pPos, void *pArg1)
{
  CUnit *pOther = static_cast<CUnit *>(pArg1);
  rect16_t rc;
  clipline_t aLines[256];
  mpoint_t  pt;
  BOOL bBlocked;

  if ((pOther->dwFlags & 0x20000)
      && (!collide_at_move_target(pOther) || pOther->bPathState == 0x0C
          || pOther->bPathState == 0x0D))
  {
    bBlocked = FALSE;
    if (net_randbetween(0, 0x1F, 0x32) >= 0x18)
      goto wander;
    goto stay;
  }
  else
  {
    bBlocked = TRUE;
    sub_42D780(pOther, &rc);
    aLines[0].ptFrom.x = rc.left;
    aLines[0].ptFrom.y = rc.top;
    aLines[0].ptTo.x = rc.right;
    aLines[0].ptTo.y = rc.top;
    aLines[1].ptFrom.x = rc.right;
    aLines[1].ptFrom.y = rc.top;
    aLines[1].ptTo.x = rc.right;
    aLines[1].ptTo.y = rc.bottom;
    aLines[2].ptFrom.x = rc.left;
    aLines[2].ptFrom.y = rc.bottom;
    aLines[2].ptTo.x = rc.right;
    aLines[2].ptTo.y = rc.bottom;
    aLines[3].ptFrom.x = rc.left;
    aLines[3].ptFrom.y = rc.top;
    aLines[3].ptTo.x = rc.left;
    aLines[3].ptTo.y = rc.bottom;

    int nLines = sub_42CB90(reinterpret_cast<intptr_t>(aLines), 4,
                            reinterpret_cast<intptr_t>(reinterpret_cast<BYTE *>(this) + offsetof(CUnit, _unmapped_106) + 2), 1);

    rc.left -= gUnitBoundBox[wType].left;
    rc.right += gUnitBoundBox[wType].right;
    rc.top -= gUnitBoundBox[wType].top;
    rc.bottom += gUnitBoundBox[wType].bottom;

    CUnit **ppList = reinterpret_cast<CUnit **>(sub_42E280(reinterpret_cast<intptr_t>(&rc),
                                                           reinterpret_cast<intptr_t>(this)));

    for (CUnit **pp = ppList; *pp != NULL; pp++)
    {
      sub_42D780(*pp, &rc);
      nLines = sub_42CB90(reinterpret_cast<intptr_t>(aLines), nLines,
                          reinterpret_cast<intptr_t>(&rc), 1);
    }

    finish_unit_iteration(ppList);

    // The clamps store straight into pt so each inner arm if-converts to a cmov.
    int nBest = 9999;

    pt.x = pSprite->wPosX;
    pt.y = pSprite->wPosY;

    point16_t ptBest = pt;

    clipline_t *pLine = aLines;

    for (int i = 0; i < nLines; i++, pLine++)
    {
      int wX = PosS(pSprite->wPosX);
      int wY = PosS(pSprite->wPosY);
      if (PosS(wX) > pLine->ptTo.x)
        pt.x = pLine->ptTo.x;
      else
        pt.x = PosS(wX) < pLine->ptFrom.x ? pLine->ptFrom.x : wX;

      if (PosS(wY) > pLine->ptTo.y)
        pt.y = pLine->ptTo.y;
      else
        pt.y = PosS(wY) < pLine->ptFrom.y ? pLine->ptFrom.y : wY;

      int nDist = veclen(PosS(pSprite->wPosX), pt.x,
                         PosS(pSprite->wPosY), pt.y);

      if (nBest > nDist)
      {
        ptBest = pt;
        nBest = nDist;
      }
    }

    if (nBest < 9999)
    {
      pt = ptBest;
      if (sub_42ED50(wType, pt.x, pt.y) && !sub_42EAB0(pt)
          && UnitCanReachGroundTarget_xy(this, pt.x, pt.y))
      {
        *pPos = pt;
        return;
      }
    }
  }

wander:
  {
    // bDirection is read before the call, into BL.
    BYTE bBase = bDirection;
    BYTE bDir = bBase + static_cast<BYTE>(net_randbetween(-3, 3, 0x32) << 4);
    int nLen = net_randbetween(2, 4, 0x32) * 4;

    if (bBlocked && (pOther->dwFlags & 2))
      nLen *= 3;

    pt.x = PosS(((DIRECTIONS[bDir * 2] * (nLen << 8)) >> 16) + pSprite->wPosX);
    pt.y = PosS(((DIRECTIONS[bDir * 2 + 1] * (nLen << 8)) >> 16) + pSprite->wPosY);

    if (!sub_42EAB0(pt) && UnitCanReachGroundTarget_xy(this, pt.x, pt.y))
    {
      *pPos = pt;
      return;
    }
  }

  {
    upos_t wY = pSprite->wPosY;
    upos_t wX = pSprite->wPosX;

    if (static_cast<unsigned>(veclen(PosS(wX), PosS(wMoveTargetX),
                                     PosS(wY), PosS(wMoveTargetY))) > 0x20
        && net_randbetween(0, 0x1F, 0x32) < 0x18)
    {
      upos_t wAY = pSprite->wPosY;
      upos_t wAX = pSprite->wPosX;
      BYTE bDir = static_cast<BYTE>(AngleBetween(PosS(wAX), PosS(wAY),
                                                 PosS(wMoveTargetX),
                                                 PosS(wMoveTargetY)));

      bDir += static_cast<BYTE>(net_randbetween(-1, 1, 0x32) << 4);

      int nLen = net_randbetween(2, 4, 0x32) * 4;

      if (bBlocked && (pOther->dwFlags & 2))
        nLen *= 3;

      pt.x = PosS(((DIRECTIONS[bDir * 2] * (nLen << 8)) >> 16) + pSprite->wPosX);
      pt.y = PosS(((DIRECTIONS[bDir * 2 + 1] * (nLen << 8)) >> 16) + pSprite->wPosY);
    }
    else
    {
      upos_t wRX = pSprite->wPosX;
      pt.x = wRX + (net_randbetween(-2, 2, 0x32) << 4);
      upos_t wRY = pSprite->wPosY;
      pt.y = wRY + (net_randbetween(-2, 2, 0x32) << 4);
    }
  }

  {
    intptr_t nHit = sub_42EAB0(pt);

    if (nHit != -1
        && (nHit == 0 || bBlocked || (reinterpret_cast<CUnit *>(nHit)->dwFlags & 0x20000))
        && UnitCanReachGroundTarget_xy(this, pt.x, pt.y))
    {
      *pPos = pt;
      return;
    }
  }

stay:
  pt.x = pSprite->wPosX;
  pt.y = pSprite->wPosY;
  *pPos = pt;
}
#endif  // OC_PAYLOAD -- FindFree_FromUnit

// 0x42D780, 170 B. The rect the other unit's box occupies once grown by this
// unit's own box, less a pixel each side so two touching boxes do not overlap.
//
// Write the other box as four whole stores, then fold this one in as four
// read-modify-writes; one fused expression per edge emits different code.
void CUnit::sub_42D780(void *pArg0, void *pArg1)
{
  CUnit *pOther = static_cast<CUnit *>(pArg0);
  rect16_t *prc = static_cast<rect16_t *>(pArg1);
  upos_t wPosY = pOther->pSprite->wPosY;
  upos_t wPosX = pOther->pSprite->wPosX;
  WORD wOtherType = pOther->wType;

  prc->top    = wPosY - gUnitBoundBox[wOtherType].top;
  prc->bottom = gUnitBoundBox[wOtherType].bottom + wPosY;
  prc->left   = wPosX - gUnitBoundBox[wOtherType].left;
  prc->right  = gUnitBoundBox[wOtherType].right + wPosX;

  prc->left   += -1 - gUnitBoundBox[wType].right;
  prc->right  += gUnitBoundBox[wType].left + 1;
  prc->top    += -1 - gUnitBoundBox[wType].bottom;
  prc->bottom += gUnitBoundBox[wType].top + 1;
}

// 0x42D830, 203 B. One step of the staged move: it is refused outright if the
// path segment is bad, committed if the staged box stays on the map, and
// otherwise turned toward the next waypoint instead.
//
// The map-edge test is GetCollisionWithTerrain's ground arm written out here,
// not a call, and all four of its failures share the turn block.
BOOL CUnit::sub_42D830()
{
  if (!sub_48EE40(PosPack(wMoveTargetX, wMoveTargetY)))  // Hyperion: packed by value (stage E)
    return TRUE;

  reinterpret_cast<CFlingy *>(this)->ProgressMovement();

  WORD wUnitType = wType;
  int x = PosS(sgwStagedX_g);

  if (x - gUnitBoundBox[wUnitType].left >= 0)
  {
    int y = PosS(sgwStagedY_g);

    if (y - gUnitBoundBox[wUnitType].top >= 0
        && gUnitBoundBox[wUnitType].right + x < gwMapPixWidth
        && gUnitBoundBox[wUnitType].bottom + y < gwMapPixHeight)
    {
      FinishUnitMovement();
      return FALSE;
    }
  }

  upos_t wPosY = pSprite->wPosY;
  upos_t wPosX = pSprite->wPosX;
  BYTE bFacing = bDirection;

  if (bFacing != AngleBetween(PosS(wPosX), PosS(wPosY),
                              PosS(wNextWaypointX),
                              PosS(wNextWaypointY)))
  {
    reinterpret_cast<CFlingy *>(this)->ProgressMovement();
    reinterpret_cast<CFlingy *>(this)->BreakTurn();
    return FALSE;
  }

  return TRUE;
}

// 0x42D900, 143 B. The first unit blocking the staged position, or NULL. The
// candidate list comes from sub_4217F0 and is closed by finish_unit_iteration
// on both exits. The five tests are one && chain whose body is `break`, so
// each jumps to the increment rather than to a block of its own.
CUnit *CUnit::GetCollisionUnit()
{
  int dx = PosS(sgwStagedX_g) - PosS(pSprite->wPosX);
  int dy = PosS(sgwStagedY_g) - pSprite->wPosY;
  CUnit **ppList = reinterpret_cast<CUnit **>(sub_4217F0(dx, dy));
  CUnit **pp = ppList;
  CUnit *pOther = *pp;

  while (pOther)
  {
    if ((pOther->bPathingFlags & 1)
        && !(pOther->dwFlags & 0x200000)
        && pOther != this
        && sub_42EBF0(reinterpret_cast<intptr_t>(pOther), reinterpret_cast<intptr_t>(this))
        && (pOther->dwFlags & 0x100000))
      break;

    pOther = *++pp;
  }

  finish_unit_iteration(ppList);
  return pOther;
}

// 0x42D990, 158 B. Whether the staged position pushes this unit off the map.
// A flyer has no terrain footprint of its own, so it is answered by sub_42DA50
// against the staged delta instead.
//
// Every bound is loaded movsx and compared signed, so the box members and the
// staged pair are read as short; the two map extents are movzx WORDs.
BOOL CUnit::GetCollisionWithTerrain()
{
  if (!(dwFlags & 0x100000))
  {
    WORD wUnitType = wType;
    int x = PosS(sgwStagedX_g);

    if (x - gUnitBoundBox[wUnitType].left < 0)
      return TRUE;

    int y = PosS(sgwStagedY_g);

    if (y - gUnitBoundBox[wUnitType].top < 0)
      return TRUE;

    if (gUnitBoundBox[wUnitType].right + x >= gwMapPixWidth)
      return TRUE;

    return gUnitBoundBox[wUnitType].bottom + y >= gwMapPixHeight;
  }

  int dx = PosS(sgwStagedX_g) - PosS(pSprite->wPosX);
  int dy = PosS(sgwStagedY_g) - pSprite->wPosY;

  return sub_42DA50(dx, dy);
}

// 0x42DA30, 20 B. The ground-only arm of GetCollisionUnit: a unit not flagged
// 0x100000 collides with nobody on the ground, so the query is skipped rather
// than answered NULL by the callee. The negated guard is what puts the
// `xor eax,eax / ret` ahead of the tail `jmp`.
CUnit *CUnit::GetGroundCollisionUnit()
{
  if (!(dwFlags & 0x100000))
    return NULL;

  return GetCollisionUnit();
}

// The collision search rect at CUnit +0x108, inside _unmapped_106. Addressed
// off `this` at every use; a rect pointer local costs a register and a frame.
static inline rect16_t *SearchRect(CUnit *pUnit)
{
  return reinterpret_cast<rect16_t *>(reinterpret_cast<BYTE *>(pUnit) + offsetof(CUnit, _unmapped_106) + 2);
}

// 0x42DA50, 262 B. Whether a step of (nDx, nDy) from the unit's position is
// clear: a target inside the cached search rect is; otherwise the rect is
// re-seated 0x80 around the unit and shrunk against the contours, and a target
// still outside it is answered by the contour walk.
int CUnit::sub_42DA50(int nDx, int nDy)
{
  CSprite *pSpr = pSprite;
  spos_t x = pSpr->wPosX + nDx;
  spos_t y = pSpr->wPosY + nDy;

  if (x >= SearchRect(this)->left && x <= SearchRect(this)->right &&
      y >= SearchRect(this)->top && y <= SearchRect(this)->bottom)
    return 0;

  contours_t *pContours = static_cast<ep_glob_t *>(epGlob)->pContours;
  SearchRect(this)->top = pSpr->wPosY - 0x80;
  SearchRect(this)->left = pSpr->wPosX - 0x80;
  SearchRect(this)->bottom = pSpr->wPosY + 0x80;
  SearchRect(this)->right = pSpr->wPosX + 0x80;
  pContours->SetSearchUnit(wType);
  pContours->sub_48B9E0(pSprite->wPosX, pSprite->wPosY, SearchRect(this));
  if (x >= SearchRect(this)->left && x <= SearchRect(this)->right &&
      y >= SearchRect(this)->top && y <= SearchRect(this)->bottom)
    return 0;
  spos_t sY = pSprite->wPosY;
  spos_t sX = pSprite->wPosX;
  return !pContours->sub_48BEC0(sX, sY, x, y);
}

// 0x42DB60, 242 B. The heading to slide along when the staged step is blocked.
// The dominant axis is the horizontal one unless the step is vertical or the
// horizontal move is itself blocked; within a case the unit's quarter-facing
// picks the angle, and 0xFF means no slide applies. The cases are written in
// the source order 3, 1, 0, 2, which sets the jump-table block order. The
// return is int: the oracle carries it in ESI, which has no byte form.
int CUnit::sub_42DB60()
{
  int nAngle = -1;
  int nQuarter = _unmapped_4A[0] >> 6;
  CSprite *pUnitSprite = pSprite;
  int nDx = PosS(sgwStagedX_g) - PosS(pUnitSprite->wPosX);
  int nDy = PosS(sgwStagedY_g) - pUnitSprite->wPosY;
  int nSide;

  if (nDx && (!nDy || sub_42DA50(nDx, 0)))
    nSide = (nDx < 0) ? 3 : 1;
  else
    nSide = (nDy < 0) ? 0 : 2;

  switch (nSide)
  {
  case 3:
    if (nQuarter == 3)
      nAngle = 0;
    else if (nQuarter == 2)
      nAngle = 0x80;
    break;

  case 1:
    if (nQuarter == 0)
      nAngle = 0;
    else if (nQuarter == 1)
      nAngle = 0x80;
    break;

  case 0:
    if (nQuarter == 0)
      nAngle = 0x40;
    else if (nQuarter == 3)
      nAngle = 0xC0;
    break;

  case 2:
    if (nQuarter == 1)
      nAngle = 0x40;
    else if (nQuarter == 2)
      nAngle = 0xC0;
    break;
  }

  return nAngle;
}

// 0x42DC70, 638 B. The heading to slide along around pOther: the side of it
// this unit is on and the unit's quarter-facing pick a heading, then the cells
// beside pOther are probed in that order. -1 when no slide applies.
int CUnit::sub_42DC70(void *pArg0)
{
  CUnit *pOther = static_cast<CUnit *>(pArg0);

  if (pOther->pSprite->flags.b & 0x20)
    return -1;

  int nAngle = -1;
  upos_t wPosY = pSprite->wPosY;
  upos_t wPosX = pSprite->wPosX;
  BYTE bHeading = AngleBetween(PosS(wPosX), PosS(wPosY),
                               PosS(wNextWaypointX), PosS(wNextWaypointY));
  int nQuarter = bHeading >> 6;

  if (get_angle(_unmapped_4A[0], bHeading) >= 0x50)
    return nAngle;

  WORD wLeft   = g_pos_sort_x[UNIT_BOUND_LEFT(pOther)].nCoord - gUnitBoundBox[wType].right - 1;
  WORD wRight  = g_pos_sort_x[UNIT_BOUND_RIGHT(pOther)].nCoord + 1 + gUnitBoundBox[wType].left;
  WORD wTop    = g_pos_sort_y[UNIT_BOUND_TOP(pOther)].nCoord - gUnitBoundBox[wType].bottom - 1;
  WORD wBottom = g_pos_sort_y[UNIT_BOUND_BOTTOM(pOther)].nCoord + 1 + gUnitBoundBox[wType].top;
  point16_t pt;

  switch (sub_42CC70(pOther))
  {
  case 3:
    pt.x = wRight;
    if (nQuarter == 3)
      nAngle = 0;
    else if (nQuarter == 2)
      nAngle = 0x80;
    break;

  case 1:
    pt.x = wLeft;
    if (nQuarter == 0)
      nAngle = 0;
    else if (nQuarter == 1)
      nAngle = 0x80;
    break;

  case 0:
    pt.y = wBottom;
    if (nQuarter == 0)
      nAngle = 0x40;
    else if (nQuarter == 3)
      nAngle = 0xC0;
    break;

  case 2:
    pt.y = wTop;
    if (nQuarter == 1)
      nAngle = 0x40;
    else if (nQuarter == 2)
      nAngle = 0xC0;
    break;
  }

  switch (nAngle)
  {
  case 0:
    pt.y = wTop;
    if (sub_42EAB0(pt))
    {
      pt.y = wBottom;
      nAngle = sub_42EAB0(pt) ? -1 : 0x80;
    }
    break;

  case 0x80:
    pt.y = wBottom;
    if (sub_42EAB0(pt))
    {
      pt.y = wTop;
      nAngle = sub_42EAB0(pt) ? -1 : 0;
    }
    break;

  case 0x40:
    pt.x = wRight;
    if (sub_42EAB0(pt))
    {
      pt.x = wLeft;
      nAngle = sub_42EAB0(pt) ? -1 : 0xC0;
    }
    break;

  case 0xC0:
    pt.x = wLeft;
    if (sub_42EAB0(pt))
    {
      pt.x = wRight;
      nAngle = sub_42EAB0(pt) ? -1 : 0x40;
    }
    break;
  }

  return nAngle;
}
