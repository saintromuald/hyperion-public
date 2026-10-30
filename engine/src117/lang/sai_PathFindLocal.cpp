// sai_PathFindLocal.cpp -- the local leg of AI pathfinding: the tile-level A*
// and its priority queue. Bodies are in oracle VA order.
//
// A callee defined in this file can be spliced into a caller in this file by
// /Ob2, which the oracle (WPO off) never does across TUs; the lever against it
// is __declspec(noinline) on the declaration.
//
// Five path_search_t fields come from this TU -- pBlocker, rcBlock, adwPath,
// nPathIndex, nPathCount -- and are declared in src117/h/sai_PathFindArea.h,
// which owns the struct. sub_491C10 gives the first two and sub_492860 the
// last three.

#include "oc_data.h"  // Hyperion: OC_VA
#include "../h/sai_PathFindArea.h"
#include "../h/sai_PathFindLocal.h"
#include "../h/sai.h"              // GetRegionPosition
#include "../h/sai_PathFindUtil.h"  // GetClosestReachable, sub_492C00
#include "../h/CSprite.h"
#include "../h/CUnit.h"
#include "../h/CUnitFind.h"         // finish_unit_iteration
#include "../h/CUnitPathUtil.h"     // IsIntersectable
#include "../h/gds_appcrit.h"       // appcritf
#include "../h/sai_PathCreate.h"     // ep_glob_t, the epGlob pointee
#include "../h/SAI_Banker.h"        // GetRegion_xy
#include "../h/CTarget.h"           // veclen
#include "../gen/globals117.h"
#include "../h/port_pathstats.h"  // Hyperion: the searches' counters
#include "../port/replays/port_snap.h"  // Hyperion: the trainer's snapshots hold it
#include "../h/CUnitBounds.h"     // Hyperion: g_bFinderSideCheck

// The local search's storage, dual-spelled one name per guard. All three are
// registered, but no marked row lists them yet, so globals117.h does not emit them.
#ifndef sgLocalPathEntries_g
#ifdef OC_DLL_MODE
#define sgLocalPathEntries_g (reinterpret_cast<pathlocaluser_t *>(OC_VA(0x69B788u)))
#elif !defined(_M_IX86)
// Hyperion: the 64-bit build's own, for a larger map's budget; a search
// fills it from the start, so nothing else reads it.
static pathlocaluser_t s_localEntries[EP_LOCAL_NODES_MAX];
#define sgLocalPathEntries_g s_localEntries
#else
extern pathlocaluser_t sgLocalPathEntries_g[250];
#endif
#endif

#ifndef sgLocalPathWorkspace_g
#ifdef OC_DLL_MODE
#define sgLocalPathWorkspace_g (*reinterpret_cast<PriorityQueue<pathlocaluser_t,150> *>(OC_VA(0x69D6C8u)))
#elif !defined(_M_IX86)
// Hyperion: the 64-bit build's own, larger than 1.17's
static PriorityQueue<pathlocaluser_t,150> s_localQueue;
PORT_SNAP_KEEP(s_localEntries);  // Hyperion: the trainer's snapshots hold them
PORT_SNAP_KEEP(s_localQueue);
#define sgLocalPathWorkspace_g s_localQueue
#elif defined(OC_PAYLOAD)
extern PriorityQueue<pathlocaluser_t,150> sgLocalPathWorkspace_g;
#else
// Static in the proof arm: sub_491F50 is 18 B short with it extern.
static PriorityQueue<pathlocaluser_t,150> sgLocalPathWorkspace_g;
#endif
#endif

#ifndef sgLocalPathContours_g
#ifdef OC_DLL_MODE
#define sgLocalPathContours_g (*reinterpret_cast<contour_workspace_t *>(OC_VA(0x69D930u)))
#else
extern contour_workspace_t sgLocalPathContours_g;
#endif
#endif

// Run the four contour passes over one search node and hand every edge they
// stacked to sub_492DE0. nStack is re-read each turn because the callee can
// push more. 1.17 types the stack contour_t on stride evidence alone and
// sub_492DE0 reads all four words of an entry as a rect; see
// Hyperion: the stack holds rects,
// as contour_t is no longer 8 bytes on x64.
void sub_491BA0(contour_workspace_t *pWs, pathlocaluser_t *pUser)
{
  BYTE bMask = pUser->b0A;

  pWs->sub_489110();
  pWs->sub_488EE0();
  pWs->sub_488320(pUser->nX, pUser->nY, bMask);
  pWs->sub_48A750();

  for (int i = 0; i < pWs->nStack; ++i)
    sub_492DE0(&pWs->stack[i]);
}

// 0x491C10 (829 B). Advance the region cursor to the unit's own region, pick the
// next waypoint, work out what is standing in the way and hand the step to
// sub_491F50. Both area-search entry points tail-jmp here.
//
BOOL sub_491C10(path_search_t *pSearch)
{
  short nCount = pSearch->nRegionCount;
  WORD nStart = pSearch->nStartRegion;
  short nCur = static_cast<short>(pSearch->nCursor + 1);
  pSearch->nCursor = nCur;

  if (nCur >= nCount)
    return FALSE;

  if (nStart != static_cast<WORD>(pSearch->nRegions[nCur]))
  {
    int i = nCur;
    while (i < nCount && nStart != static_cast<WORD>(pSearch->nRegions[i]))
      ++i;

    if (i >= nCount)
      return FALSE;

    pSearch->nCursor = static_cast<short>(i);
  }

  int nLeft = nCount - pSearch->nCursor;
  point16_t ptWay;
  int bDirect;
  CUnit **ppList;
  int nRegion;

  if (nLeft == 1 || nLeft == 2 || nLeft <= 3)
  {
    bDirect = 1;
    ptWay = pSearch->ptCur;
  }
  else
  {
    bDirect = 0;
    GetRegionPosition(static_cast<WORD>(pSearch->nRegions[pSearch->nCursor + 2]), &ptWay);
  }

  if (pSearch->nCursor == pSearch->nRegionCount - 1)
  {
    // Two 0x1FFE stores; a third gives nRegion a reference more and moves
    // its frame slot above ppList's.
    if (nStart != pSearch->nGoalRegion)
    {
      if (GetClosestReachable(pSearch, nStart))
      {
        bDirect = 1;
        nRegion = 0x1FFE;
      }
      else
      {
        appcritf("error in GetClosestReachable");
        goto closest;
      }
    }
    else
    {
closest:
      nRegion = 0x1FFE;
    }
  }
  else
  {
    nRegion = static_cast<WORD>(pSearch->nRegions[pSearch->nCursor + 1]);
  }

  pSearch->pBlocker = NULL;

  if (bDirect)
  {
    pSearch->pBlocker = *reinterpret_cast<CUnit **>(pSearch->pUnit->_unmapped_14);

    if (pSearch->pBlocker == NULL)
    {
      ppList = reinterpret_cast<CUnit **>(
          pSearch->pUnit->sub_421C80(static_cast<WORD>(pSearch->ptCur.x),
                                     static_cast<WORD>(pSearch->ptCur.y)));
      CUnit **pp = ppList;
      CUnit *pIt = *ppList;

      while (pIt != NULL)
      {
        if (IsIntersectable(reinterpret_cast<intptr_t>(pIt),
                            reinterpret_cast<intptr_t>(pSearch->pUnit)))
        {
          pSearch->pBlocker = pIt;
          break;
        }

        pIt = *++pp;
      }

      finish_unit_iteration(ppList);
    }

    if (pSearch->pBlocker != NULL && pSearch->pBlocker->IsCircleOfPower())
      pSearch->pBlocker = NULL;

    CUnit *pBlocker = pSearch->pBlocker;

    if (pBlocker != NULL)
    {
      // Stored through a pointer and grown through the struct: the grow stays a
      // read-modify-write on memory and pUnit is loaded after the stores.
      mrect_t *prc = &pSearch->rcBlock;  // Hyperion: a box of 4-byte positions on x64 (stage E)
      CSprite *pSprite = pBlocker->pSprite;
      WORD wY = pSprite->wPosY;
      WORD wX = pSprite->wPosX;
      WORD wType = pBlocker->wType;
      prc->top = wY - gUnitBoundBox[wType].top;
      prc->bottom = gUnitBoundBox[wType].bottom + wY;
      prc->left = wX - gUnitBoundBox[wType].left;
      prc->right = gUnitBoundBox[wType].right + wX;
      CUnit *pMe = pSearch->pUnit;
      pSearch->rcBlock.left += -1 - gUnitBoundBox[pMe->wType].right;
      pSearch->rcBlock.right += gUnitBoundBox[pMe->wType].left + 1;
      pSearch->rcBlock.top += -1 - gUnitBoundBox[pMe->wType].bottom;
      pSearch->rcBlock.bottom += gUnitBoundBox[pMe->wType].top + 1;

      int nX = pSearch->ptStart.x;
      int nY = pSearch->ptStart.y;
      int nRight2 = pSearch->rcBlock.right;
      int nLeft2 = pSearch->rcBlock.left;
      int nTop2 = pSearch->rcBlock.top;
      int nBottom2 = pSearch->rcBlock.bottom;

      int bVert = (nX == nLeft2 || nX == nRight2) ? 1 : 0;
      int bHorz = (nY == nTop2 || nY == nBottom2) ? 1 : 0;

      if ((bVert && nY >= nTop2 && nY <= nBottom2) ||
          (bHorz && nX >= nLeft2 && nX <= nRight2))
      {
        pSearch->adwPath[0] = PosPack(pSearch->ptStart.x, pSearch->ptStart.y);  // Hyperion: packed by value (stage E)
        pSearch->ptCur = pSearch->ptStart;
        pSearch->nPathIndex = 0;
        pSearch->nPathCount = 1;
        pSearch->b01E = 1;
        pSearch->bClosest = 0;
        return TRUE;
      }
    }
    else
    {
      pSearch->rcBlock.left = -32000;
      pSearch->rcBlock.top = -32000;
      pSearch->rcBlock.right = -32000;
      pSearch->rcBlock.bottom = -32000;

      point16_t pt = ptWay;

      if (sub_492C00(pSearch, &pt))
      {
        if (ptWay.x != pt.x || ptWay.y != pt.y)
        {
          ptWay = pt;
          pSearch->ptCur = pt;
        }
      }
    }
  }

  BOOL bOk = sub_491F50(pSearch, nRegion, ptWay);

  if (bOk)
  {
    DWORD dwLast = pSearch->adwPath[static_cast<short>(pSearch->nPathCount) - 1];

    if (pSearch->b01E)
    {
      if (bDirect == 0 &&
          static_cast<short>(dwLast) == ptWay.x &&
          static_cast<short>(dwLast >> 16) == ptWay.y)
      {
        pSearch->b01E = 0;
        return bOk;
      }
    }
    else
    {
      if (static_cast<short>(nRegion) != static_cast<short>(0x1FFE))
        return bOk;

      // Identical arms: the test is one more bDirect reference, which puts its
      // frame slot above ppList's and nRegion's.
      if (bDirect)
      {
        pSearch->b01E = 1;
        pSearch->bClosest = 1;
      }
      else
      {
        pSearch->b01E = 1;
        pSearch->bClosest = 1;
      }
    }

    pSearch->ptCur = PosPoint(PosPackedX(dwLast), PosPackedY(dwLast));  // Hyperion: by field (stage E)
  }

  return bOk;
}

// Hyperion: the search's entries by their point. sub_491F50 looked each
// contour mark's point up among its entries by walking them from the second, every mark
// against every entry so far: on a large map, of 1,000 entries, most of a battle's local
// search. An entry's point is set as it is made and kept, and no two entries from the second
// on have the same one (a mark at an entry's point finds it and makes none), so a table of
// their points gives the walk's answer: the entry with the point, or none. A search's
// entries are the ones of its own number. HYPERION_FINDER_CHECK=1 walks as well and compares.
struct local_point_t {
  DWORD dwPoint;
  unsigned nSearch;  // the search the slot was filled in
  pathlocaluser_t *pEntry;
};
static const int LOCAL_POINTS_BITS = 12;  // 4,096 slots, past twice EP_LOCAL_NODES_MAX
static local_point_t s_localPoints[1 << LOCAL_POINTS_BITS];
static unsigned s_nLocalSearch = 0;

static local_point_t *LocalPointSlot(DWORD dwPoint)
{
  unsigned h = (dwPoint * 2654435761u) >> (32 - LOCAL_POINTS_BITS);
  for (;; h = (h + 1) & ((1u << LOCAL_POINTS_BITS) - 1)) {
    local_point_t *pSlot = &s_localPoints[h];
    if (pSlot->nSearch != s_nLocalSearch || pSlot->dwPoint == dwPoint)
      return pSlot;
  }
}

// 0x491F50 (2305 B). The local tile A*: seed the start tile, pop the cheapest,
// turn its contour marks into new or re-costed tiles, and walk the best tile's
// parents back into the request; when the search gives up it picks a fallback tile.
// The match needs the static queue and both merged pairs: the new node's pair keeps
// nY256's load after the point copy, and the re-insert pair's pCur read ranks pCur's
// reload ahead of nY256 for ECX.
BOOL sub_491F50(path_search_t *pSearch, int nRegion, point16_t pt)
{
  BOOL bAtCur = pt.x == pSearch->ptCur.x && pt.y == pSearch->ptCur.y;  // Hyperion: by field (stage E)
  ep_glob_t *pGlob = static_cast<ep_glob_t *>(epGlob);
  BOOL bPassable = static_cast<WORD>(nRegion) < AI_AREAS_117 && !IsInaccessibleRegion(static_cast<WORD>(nRegion));

  pSearch->n166 = 0;
  pSearch->n16A = 0;
  ++g_pathStats.nLocal;  // Hyperion
  WORD wStart = pSearch->nStartRegion;
  WORD wCur = GetRegion_xy(pSearch->ptCur.x, pSearch->ptCur.y);

  WORD *pPaths;
  int nPaths = GetRegionPaths(static_cast<WORD>(nRegion) < AI_AREAS_117 ? static_cast<WORD>(nRegion) : wCur, &pPaths);
  WORD *pw = pPaths;
  for (int i = 0; i < nPaths; ++i, ++pw)
    if (*pw != wStart)
      pGlob->aRegions[*pw].nMark = 1;

  int nGoalX = pt.x << 8;
  int nGoalY = pt.y << 8;

  sgLocalPathContours_g.sub_488E10(static_cast<ep_glob_t *>(epGlob)->pContours);
  sgLocalPathContours_g.SetSearchUnit(pSearch->pUnit->wType);
  const rect16_t rcBlock = pSearch->rcBlock;  // Hyperion: 1.17's box, which the contours take (stage E)
  sgLocalPathContours_g.sub_4892D0(reinterpret_cast<intptr_t>(pSearch->pUnit), &pt,
                                   reinterpret_cast<intptr_t>(pSearch->pBlocker), &rcBlock);
  ResetBins();

  // Hyperion: the map's budget, 1.17's 250 entries and 150 in the queue on a 1.17
  // map; the two points it gives up at are 1.17's 150 and 200 of the 250
  const int nNodesMax = g_epLocalNodes;
  const int nQueueMax = g_epLocalQueue;
  const int nGiveUpTarget = nNodesMax * 3 / 5;
  const int nGiveUpMarked = nNodesMax * 4 / 5;

  pathlocaluser_t sentinel;
  sgLocalPathWorkspace_g.m_count = 0;
  sgLocalPathWorkspace_g.m_pSentinel = &sentinel;

  pathlocaluser_t *pCur = &sgLocalPathEntries_g[0];
  *reinterpret_cast<DWORD *>(pCur) = PosPack(pSearch->ptStart.x, pSearch->ptStart.y);  // Hyperion: packed by value (stage E)
  pCur->nRegion = wStart;
  sentinel.nKey = -1;
  pathlocaluser_t *pNext = &sgLocalPathEntries_g[1];
  pCur->pParent = NULL;
  pCur->bGoal = 0;
  pCur->bClosed = 0;
  pCur->b0A = 0xFF;
  pCur->nDepth = 0;
  pCur->nH = 0x400000;
  pCur->nG = 0;
  pCur->nKey = 0x400000;
  pCur->bMarked = 0;
  pCur->bTarget = 0;
  sgLocalPathWorkspace_g.heapInsert(pCur);

  BOOL bFound = FALSE;
  BOOL bGiveUp = FALSE;
  int nTargetOpen = 0;
  int nMarkedOpen = 0;

  // Hyperion: the entries by their point (LocalPointSlot), once a new search's
  // number has left every slot of the last one; where its entries could fill more than half
  // the table, the walk
  const bool bPoints = nNodesMax * 2 <= (1 << LOCAL_POINTS_BITS);
  if (++s_nLocalSearch == 0) {
    for (local_point_t &slot : s_localPoints)
      slot.nSearch = 0;
    s_nLocalSearch = 1;
  }

  while (sgLocalPathWorkspace_g.m_count != 0)
  {
    pCur = sgLocalPathWorkspace_g.m_elems[1];
    sgLocalPathWorkspace_g.m_elems[1] = sgLocalPathWorkspace_g.m_elems[sgLocalPathWorkspace_g.m_count--];
    if (pCur->bGoal) { sgLocalPathWorkspace_g.heapifyDown(1); } else { sgLocalPathWorkspace_g.heapifyDown(1); }

    if (pCur->bGoal)
    {
      pSearch->b01E = 1;
      goto done;
    }

    if (pCur->b0A)
      sub_4930B0(reinterpret_cast<point16_t *>(pCur), &pCur->b0A);

    if (nTargetOpen > 0 && !bFound && (!bAtCur || pNext - sgLocalPathEntries_g >= nGiveUpTarget))
      bGiveUp = TRUE;
    if (nMarkedOpen != 0 && !bFound && pNext - sgLocalPathEntries_g >= nGiveUpMarked)
      bGiveUp = TRUE;

    if (pCur->bTarget && bGiveUp)
      goto done;

    pCur->bClosed = 1;
    if (pCur->bTarget)
      --nTargetOpen;
    if (pCur->bMarked)
      --nMarkedOpen;
    if (pCur->bMarked && bGiveUp)
      break;

    if (!bFound && !bGiveUp && pCur->b0A)
    {
      sub_491BA0(&sgLocalPathContours_g, pCur);

      // The goal test reads bB a fourth time; that is what biases the pointer to bB.
      contour_mark_t *pMark = sgLocalPathContours_g.marks;
      for (int nMarks = sgLocalPathContours_g.nMarks; nMarks > 0; --nMarks, ++pMark)
      {
        point16_t ptMark = pMark->pt;
        int x = ptMark.x;
        int wReg = GetRegion_xy(x, ptMark.y);
        BYTE bInTarget = static_cast<WORD>(wReg) == static_cast<WORD>(nRegion);
        BYTE bMarked = pGlob->aRegions[wReg].nMark != 0;

        if (pMark->bB)
        {
          bFound = TRUE;
        }
        else if (pMark->bA)
        {
          sub_4930B0(&pMark->pt, &pMark->bA);
          if (!pMark->bA)
            continue;
        }

        BYTE bMask = pMark->bA;
        pathlocaluser_t *p;
        local_point_t *pSlot = NULL;  // Hyperion
        if (bPoints)
        {
          pSlot = LocalPointSlot(*reinterpret_cast<DWORD *>(&ptMark));
          p = pSlot->nSearch == s_nLocalSearch ? pSlot->pEntry : pNext;
        }
        if (!bPoints || g_bFinderSideCheck)
        {
          pathlocaluser_t *pFound = p;
          for (p = &sgLocalPathEntries_g[1]; p < pNext; ++p)
            if (*reinterpret_cast<DWORD *>(&ptMark) == *reinterpret_cast<DWORD *>(p))
              break;
          if (bPoints && pFound != p)
            FinderSideDiffer("a local search's points", static_cast<int>(p - sgLocalPathEntries_g));
        }

        if (p != pNext && p != NULL)
        {
          if (pCur->pParent == p)
            continue;

          // The argument locals set the order the four shifts are scheduled in.
          int nX256 = x << 8;
          int nCurY = static_cast<short>(pCur->nY) << 8;
          int nCurX = static_cast<short>(pCur->nX) << 8;
          int nCost = veclen(nX256, nCurX, ptMark.y << 8, nCurY);
          if (bPassable && static_cast<WORD>(wReg) != static_cast<WORD>(nRegion) && static_cast<WORD>(wReg) != wStart)
            nCost = nCost * 512 / 256;

          int nG = pCur->nG + nCost;
          if (nG >= p->nG)
          {
            if (!p->bClosed)
              p->b0A &= bMask;
            continue;
          }

          p->pParent = pCur;
          p->nDepth = pCur->nDepth + 1;
          p->nG = nG;
          int nKey = p->nH + nG;
          if (!p->bClosed)
          {
            sgLocalPathWorkspace_g.heapUpdate(p, nKey);
          }
          else
          {
            p->nKey = nKey;
            p->b0A = bMask;
            p->bClosed = 0;
            p->bGoal = pMark->bB;
            sgLocalPathWorkspace_g.heapInsert(p);
            if (bInTarget)
              ++nTargetOpen;
            if (bMarked)
              ++nMarkedOpen;
          }

          if (sgLocalPathWorkspace_g.m_count == nQueueMax)
            break;
        }
        else
        {
          p = pNext++;
          x <<= 8;
          *reinterpret_cast<DWORD *>(p) = *reinterpret_cast<DWORD *>(&ptMark);
          if (pSlot != NULL)  // Hyperion
          {
            pSlot->dwPoint = *reinterpret_cast<DWORD *>(&ptMark);
            pSlot->nSearch = s_nLocalSearch;
            pSlot->pEntry = p;
          }
          int nY256 = ptMark.y << 8;
          // Identical arms; the block edge keeps nY256's load here, where only ESI is free.
          if (p->b0A) { p->pParent = pCur; } else { p->pParent = pCur; }
          p->nRegion = wReg;
          p->nDepth = pCur->nDepth + 1;
          p->b0A = bMask;

          int nCurX = static_cast<short>(pCur->nX) << 8;
          int nCurY = static_cast<short>(pCur->nY) << 8;
          int nCost = veclen(x, nCurX, nY256, nCurY);
          if (bPassable && static_cast<WORD>(wReg) != static_cast<WORD>(nRegion) && static_cast<WORD>(wReg) != wStart)
            nCost = nCost * 512 / 256;

          p->nH = veclen(x, nGoalX, ptMark.y << 8, nGoalY);
          p->nG = pCur->nG + nCost;
          p->nKey = p->nH + p->nG;
          p->bClosed = bMask == 0;
          p->bGoal = pMark->bB;
          p->bMarked = bMarked;
          p->bTarget = bInTarget;

          if (!p->bClosed || pMark->bB)
          {
            p->bClosed = 0;
            sgLocalPathWorkspace_g.heapInsert(p);
            if (bInTarget)
              ++nTargetOpen;
            if (bMarked)
              ++nMarkedOpen;
          }

          if (sgLocalPathWorkspace_g.m_count == nQueueMax || pNext == &sgLocalPathEntries_g[nNodesMax])
            break;
        }
      }

      if (pSearch->n16A < sgLocalPathWorkspace_g.m_count)
        pSearch->n16A = static_cast<short>(sgLocalPathWorkspace_g.m_count);

      if ((sgLocalPathWorkspace_g.m_count == nQueueMax || pNext == &sgLocalPathEntries_g[nNodesMax]) &&
          !bFound && nTargetOpen == 0)
      {
        ++g_pathStats.nLocalCapped;  // Hyperion
        break;
      }
    }
  }

  {
    pSearch->b01D = 1;
    int nOpen = 0;
    int nOpenHere = 0;
    pathlocaluser_t *p;
    for (p = &sgLocalPathEntries_g[1]; p < pNext; ++p)
    {
      ep_region_t *pr = &pGlob->aRegions[p->nRegion];
      if (pr->nMark)
        ++pr->nMark;

      if (p->bClosed)
        continue;

      if (p->b0A)
        sub_4930B0(reinterpret_cast<point16_t *>(p), &p->b0A);

      if (p->b0A)
      {
        ++nOpen;
        if (p->nRegion == wCur)
          ++nOpenHere;
      }
      else
      {
        p->bClosed = 1;
        if (p->bTarget)
          --nTargetOpen;
      }
    }

    if (bAtCur)
    {
      nOpen = nOpenHere;
      int i;
      WORD *pw = pPaths;
      for (i = 0; i < nPaths; ++i, ++pw)
      {
        if (*pw == wStart)
          continue;
        int nMark = pGlob->aRegions[*pw].nMark;
        if (nMark < 2)
          break;
        nOpen -= nMark / 2;
        if (nOpen < 0)
          nOpen = 0;
      }
      if (i < nPaths)
        ++nOpen;
    }

    BOOL bStuck = nOpen == 0 && nTargetOpen == 0;
    pCur = &sgLocalPathEntries_g[0];
    int nX0 = static_cast<short>(sgLocalPathEntries_g[0].nX) << 8;
    sgLocalPathEntries_g[0].nH = veclen(nX0, nGoalX, static_cast<short>(sgLocalPathEntries_g[0].nY) << 8, nGoalY);

    if (bStuck)
    {
      int nBest = nTargetOpen ? 0x400000 : sgLocalPathEntries_g[0].nH;
      for (p = &sgLocalPathEntries_g[1]; p < pNext; ++p)
      {
        if (p->nH < nBest &&
            (bAtCur || (nTargetOpen && p->nRegion == wCur) || (!nTargetOpen && p->nRegion == wStart)))
        {
          pCur = p;
          nBest = p->nH;
        }
      }
      pSearch->b01E = 1;
      pSearch->bClosest = 1;
      ++g_pathStats.nLocalShort;  // Hyperion
    }
    else
    {
      int nBest = 0x400000;
      for (p = &sgLocalPathEntries_g[1]; p < pNext; ++p)
      {
        int nH = p->nH;
        int nScore;
        if (p->nRegion == wCur || p->nRegion == static_cast<WORD>(nRegion))
          nScore = p->nG / 2 + nH;
        else if (pGlob->aRegions[p->nRegion].nMark)
          nScore = nH * 3 / 2;
        else if (p->nRegion == wStart)
          nScore = nH * 2;
        else
          nScore = nH * 4;

        if (p->bClosed)
          nScore *= 32;

        if (nScore < nBest)
        {
          pCur = p;
          nBest = nScore;
        }
      }
    }
  }

done:
  pSearch->n166 = static_cast<short>(pNext - sgLocalPathEntries_g);
  g_pathStats.nLocalNodes += static_cast<unsigned>(pNext - sgLocalPathEntries_g);  // Hyperion
  if (pCur)
  {
    DWORD adwSteps[128];
    DWORD *pOut = adwSteps;
    int n;
    for (n = 0; pCur != NULL && n < 128; ++n)
    {
      *pOut++ = *reinterpret_cast<DWORD *>(pCur);
      pCur = pCur->pParent;
    }
    sub_492860(pSearch, n, adwSteps);
  }

  // Guarded so the path pointer is loaded after the count test, as in the oracle.
  if (nPaths > 0)
  {
    pw = pPaths;
    for (int i = 0; i < nPaths; ++i, ++pw)
      if (*pw != wStart)
        pGlob->aRegions[*pw].nMark = 0;
  }

  return static_cast<short>(pSearch->nPathCount) > 0;
}

// Copy the step array back into the request, walking it from the far end and
// dropping the last step, then close it with the caller's own first entry. The
// 0x31 cap is what makes the 50-dword array safe.
// The count is assigned before the loop and the index in the for header, which
// is what puts the count in ECX (`lea ecx, [edx-3h]`) and the index in EDX.
void sub_492860(path_search_t *pFind, int nCount, const DWORD *pPath)
{
  if (nCount < 3)
  {
    if (nCount == 2)
      pFind->adwPath[0] = pPath[0];

    pFind->nPathIndex = 0;
    pFind->nPathCount = static_cast<WORD>(nCount - 1);
    return;
  }

  int nLeft = nCount - 3;
  int i;
  for (i = 0; nLeft >= 0; --nLeft)
  {
    if (i >= 0x31 || nLeft < 0)
      break;
    pFind->adwPath[i++] = pPath[nLeft + 1];
  }

  pFind->adwPath[i] = pPath[0];
  pFind->nPathIndex = 0;
  pFind->nPathCount = static_cast<WORD>(i + 1);
}

// A stub. Every caller sets up four arguments and reads EAX; this build answers
// 0 to all of them.
int sub_4928F0(int, int, int, int)
{
  return 0;
}

// Re-key an element already in the heap and restore the invariant. The element
// is found by a linear scan from the tail, so slot 0 doubles as the not-found
// answer; a key that fell sifts up here, a key that rose goes to heapifyDown.
// The sift-up runs on `n`, a copy of `i`: with `i` itself the allocator bridges
// the two exits with a `mov` instead of duplicating the tail, 11 B short.
void PriorityQueue<pathlocaluser_t, 150>::heapUpdate(pathlocaluser_t *pElem, int nKey)
{
  int nOld = pElem->nKey;
  if (nOld == nKey)
    return;

  pElem->nKey = nKey;

  int i;
  for (i = m_count; i > 0; --i)
    if (m_elems[i] == pElem)
      break;

  if (i == 0)
    return;

  if (nOld > nKey)
  {
    int n = i;
    pElem = m_elems[n];
    m_elems[0] = m_pSentinel;

    while (m_elems[n / 2]->nKey >= pElem->nKey)
    {
      m_elems[n] = m_elems[n / 2];
      n /= 2;
    }

    m_elems[n] = pElem;
  }
  else
  {
    heapifyDown(i);
  }
}

// Sift the element at `i` down until both children outrank it. The loop test is
// rotated -- the oracle tests `i <= m_count / 2` once before entering and again
// at the bottom -- so the final store appears three times, once per exit, each
// from whichever register still holds the saved element.
void PriorityQueue<pathlocaluser_t, 150>::heapifyDown(int i)
{
  pathlocaluser_t *pElem = m_elems[i];

  while (i <= m_count / 2)
  {
    int nChild = i * 2;
    if (nChild < m_count && m_elems[nChild]->nKey > m_elems[nChild + 1]->nKey)
      ++nChild;

    if (pElem->nKey <= m_elems[nChild]->nKey)
      break;

    m_elems[i] = m_elems[nChild];
    i = nChild;
  }

  m_elems[i] = pElem;
}

// Append at the tail and sift up. The sentinel parked in slot 0 is what lets
// the loop run without an index test.
void PriorityQueue<pathlocaluser_t, 150>::heapInsert(pathlocaluser_t *pElem)
{
  ++m_count;
  m_elems[m_count] = pElem;

  int i = m_count;
  pathlocaluser_t *pNew = m_elems[i];
  m_elems[0] = m_pSentinel;

  while (m_elems[i / 2]->nKey >= pNew->nKey)
  {
    m_elems[i] = m_elems[i / 2];
    i /= 2;
  }

  m_elems[i] = pNew;
}
