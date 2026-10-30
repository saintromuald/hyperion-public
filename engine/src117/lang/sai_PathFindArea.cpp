// sai_PathFindArea.cpp -- the area-level A* and its priority queue. Bodies are
// in oracle VA order.

#include "oc_data.h"  // Hyperion: OC_VA
#include "../h/port_pathstats.h"  // Hyperion: the searches' counters
#include "../h/sai_PathFindArea.h"
#include "../h/CUnit.h"
#include "../h/SAI_Banker.h"     // GetRegion_xy
#include "../h/sai_TownBuilder.h"  // GetAreaIndex
#include "../h/CTarget.h"        // veclen
#include "../h/sai_PathFindLocal.h"  // sub_491C10, the tail both entries jump to
#include "../h/sai_PathCreate.h"    // ep_glob_t, ep_region_t
#include "../gen/globals117.h"
#include "../port/replays/port_snap.h"  // Hyperion: the trainer's snapshots hold it

// Open an area search: resolve the two regions and seat the unit's collision
// box, then either take the one-region shortcut or run the search. The tail is
// one call on a flag, and the four empty labels keep the ptCur copy where 1.17
// has it.
BOOL sub_491210(path_search_t *pSearch)
{
  pSearch->nStartRegion = GetRegion_xy(pSearch->ptStart.x, pSearch->ptStart.y);
  pSearch->nGoalRegion = GetRegion_xy(pSearch->ptGoal.x, pSearch->ptGoal.y);

  pSearch->b01C = 0;
  pSearch->b01D = 0;
  pSearch->b01E = 0;
  pSearch->bClosest = 0;
L_1:;
L_2:;
L_3:;
L_4:;
  pSearch->ptCur = pSearch->ptGoal;

  pSearch->rcBound.left = (short)-gUnitBoundBox[pSearch->pUnit->wType].left;
  pSearch->rcBound.top = (short)-gUnitBoundBox[pSearch->pUnit->wType].top;
  pSearch->rcBound.right = gUnitBoundBox[pSearch->pUnit->wType].right;
  pSearch->rcBound.bottom = gUnitBoundBox[pSearch->pUnit->wType].bottom;

  pSearch->nNodes = 0;
  pSearch->n166 = 0;
  pSearch->nQueuePeak = 0;
  pSearch->n16A = 0;

  BOOL bOk;
  if (pSearch->nStartRegion == pSearch->nGoalRegion)
  {
    pSearch->nRegions[0] = pSearch->nStartRegion;
    pSearch->nCursor = -1;
    pSearch->nRegionCount = 1;
    pSearch->bChainLen = 1;
    bOk = TRUE;
  }
  else
    bOk = sub_491300(pSearch);
  return bOk ? sub_491C10(pSearch) : 0;
}

// Sift the element at `i` up until its parent outranks it. 1.17 has no body
// for it; __forceinline because MSVC declines a plain inline member here.
__forceinline void PriorityQueue<areauser_t,125>::heapifyUp(int i)
{
  areauser_t *pElem = m_elems[i];

  m_elems[0] = m_pSentinel;
  while (m_elems[i / 2]->nKey >= pElem->nKey)
  {
    m_elems[i] = m_elems[i / 2];
    i /= 2;
  }

  m_elems[i] = pElem;
}

#if !defined(OC_PAYLOAD) || defined(OC_WITH_HELD)
#ifdef OC_DLL_MODE
#define sgAreaFinderWorkspace_g (*reinterpret_cast<areaqueue_t *>(OC_VA(0x69B588u)))
#elif !defined(_M_IX86)
// Hyperion: the 64-bit build's own, larger than 1.17's at 0x69B588; a
// search empties it first, so nothing else reads it.
static areaqueue_t s_areaQueue;
PORT_SNAP_KEEP(s_areaQueue);  // Hyperion: the trainer's snapshots hold it
#define sgAreaFinderWorkspace_g s_areaQueue
#else
extern areaqueue_t sgAreaFinderWorkspace_g;   // 0x69B588
#endif

// Store the key, reopen the node, append it and sift up.
__forceinline void PriorityQueue<areauser_t,125>::heapInsert(areauser_t *pElem, int nKey)
{
  pElem->nKey = nKey;
  pElem->bClosed = 0;
  ++m_count;
  m_elems[m_count] = pElem;
  heapifyUp(m_count);
}

// veclen through int parameters, which loads the arguments in 1.17's order.
static __forceinline int area_len(int x1, int x2, int y1, int y2)
{
  return veclen(x1, x2, y1, y2);
}

// A region's nMark dword holds its search node while a search runs.
static __forceinline areauser_t *&region_user(ep_region_t *pRgn)
{
  return *reinterpret_cast<areauser_t **>(&pRgn->nMark);
}

// Run the area-level A* between the two regions, fall back to the node that
// got closest when the heap or the node pool fills, and rerun once forward
// from the region a backward search stopped on. The rerun is a goto to a label
// above the two counter resets, and the re-key branch assigns Y before X in its
// start and goal arms, both as in 1.17.
BOOL sub_491300(path_search_t *pSearch)
{
  // Hyperion: the nodes of the map's budget, 350 on a 1.17 map, and the sentinel
  // after them; one search runs at a time.
  static areauser_t s_aNodes[EP_AREA_NODES_MAX + 1];
  PORT_SNAP_KEEP(s_aNodes);  // Hyperion: the trainer's snapshots hold it
  areauser_t *const aNodes = s_aNodes;
  const int nNodesMax = g_epAreaNodes;
  const int nQueueMax = g_epAreaQueue;

  if (pSearch->nStartRegion == pSearch->nGoalRegion)
    return FALSE;

  WORD nStart = pSearch->nStartRegion;
  pSearch->b01C = 0;
  WORD nGoal = pSearch->nGoalRegion;
  int bRetry = 0;
  ++g_pathStats.nArea;  // Hyperion

  {
  restart:
    pSearch->nNodes = 0;
    pSearch->nQueuePeak = 0;
    WORD wStartArea = GetAreaIndex(nStart);
    BOOL bForward = (wStartArea == GetAreaIndex(nGoal) || bRetry) ? TRUE : FALSE;

    int nStartX = pSearch->ptStart.x << 8;
    int nStartY = pSearch->ptStart.y << 8;
    int nGoalX = pSearch->ptGoal.x << 8;
    int nGoalY = pSearch->ptGoal.y << 8;
    int nTargetX;
    int nTargetY;
    WORD nFrom;
    WORD nTo;
    if (bForward)
    {
      nTargetX = nGoalX;
      nTargetY = nGoalY;
      nFrom = nStart;
      nTo = nGoal;
    }
    else
    {
      nTargetX = nStartX;
      nTargetY = nStartY;
      nFrom = nGoal;
      nTo = nStart;
    }

    sgAreaFinderWorkspace_g.m_pSentinel = &aNodes[nNodesMax];
    areauser_t *pFree = &aNodes[1];
    aNodes[0].nAreaId = nFrom;
    ep_region_t *pRgn0 = &static_cast<ep_glob_t *>(epGlob)->aRegions[nFrom];
    sgAreaFinderWorkspace_g.m_count = 0;
    aNodes[0].pArea = pRgn0;
    aNodes[nNodesMax].nKey = -1;
    aNodes[0].pPrev = NULL;
    aNodes[0].nToGoal = 0x400000;
    aNodes[0].nCost = 0;
    aNodes[0].nKey = 0x400000;
    region_user(pRgn0) = &aNodes[0];
    aNodes[0].bClosed = 0;
    ++sgAreaFinderWorkspace_g.m_count;
    sgAreaFinderWorkspace_g.m_elems[sgAreaFinderWorkspace_g.m_count] = &aNodes[0];
    sgAreaFinderWorkspace_g.heapifyUp(sgAreaFinderWorkspace_g.m_count);

    areauser_t *pFound = NULL;
    while (sgAreaFinderWorkspace_g.m_count != 0)
    {
      areauser_t *pCur = sgAreaFinderWorkspace_g.m_elems[1];
      sgAreaFinderWorkspace_g.m_elems[1] =
          sgAreaFinderWorkspace_g.m_elems[sgAreaFinderWorkspace_g.m_count];
      --sgAreaFinderWorkspace_g.m_count;
      sgAreaFinderWorkspace_g.heapifyDown(1);

      if (pCur->nAreaId == nTo)
      {
        pFound = pCur;
        break;
      }

      ep_region_t *pRgn = static_cast<ep_region_t *>(pCur->pArea);
      pCur->bClosed = 1;
      const WORD *pPaths = pRgn->pPaths;
      int nPaths = pRgn->bPathFirst;
      if (pRgn->wArea != wStartArea)
        nPaths = pRgn->bPathCount;

      int nCurX;
      int nCurY;
      if (pCur->nAreaId == nStart)
      {
        nCurX = nStartX;
        nCurY = nStartY;
      }
      else if (pCur->nAreaId == nGoal)
      {
        nCurX = nGoalX;
        nCurY = nGoalY;
      }
      else
      {
        nCurX = pRgn->nX;
        nCurY = pRgn->nY;
      }

      for (; nPaths > 0; --nPaths, ++pPaths)
      {
        short nNext = *pPaths;
        ep_region_t *pNextRgn = &static_cast<ep_glob_t *>(epGlob)->aRegions[(WORD)nNext];
        areauser_t *pNode = region_user(pNextRgn);
        if (pNode == NULL)
        {
          areauser_t *pNew = pFree++;
          pNew->nAreaId = nNext;
          pNew->pPrev = pCur;
          pNew->pArea = pNextRgn;
          region_user(pNextRgn) = pNew;

          int nX;
          int nY;
          if (nNext == nStart)
          {
            nX = nStartX;
            nY = nStartY;
          }
          else if (nNext == nGoal)
          {
            nX = nGoalX;
            nY = nGoalY;
          }
          else
          {
            nX = pNextRgn->nX;
            nY = pNextRgn->nY;
          }

          int nStep = veclen(nCurX, nX, nCurY, nY);
          if (pNextRgn->wArea != wStartArea)
            nStep = nStep * 512 / 256;
          pNew->nToGoal = veclen(nX, nTargetX, nY, nTargetY);
          pNew->nCost = pCur->nCost + nStep;
          sgAreaFinderWorkspace_g.heapInsert(pNew, pNew->nToGoal + pNew->nCost);

          if (sgAreaFinderWorkspace_g.m_count == nQueueMax || pFree == &aNodes[nNodesMax])
            break;
        }
        else if (pCur->pPrev != pNode)
        {
          int nX;
          int nY;
          if (pNode->nAreaId == nStart)
          {
            nY = nStartY;
            nX = nStartX;
          }
          else if (pNode->nAreaId == nGoal)
          {
            nY = nGoalY;
            nX = nGoalX;
          }
          else
          {
            nX = static_cast<ep_region_t *>(pNode->pArea)->nX;
            nY = static_cast<ep_region_t *>(pNode->pArea)->nY;
          }

          int nStep = veclen(nX, nCurX, nY, nCurY);
          if (static_cast<ep_region_t *>(pNode->pArea)->wArea != wStartArea)
            nStep = nStep * 512 / 256;
          if (pCur->nCost + nStep < pNode->nCost)
          {
            pNode->pPrev = pCur;
            pNode->nCost = pCur->nCost + nStep;
            int nKey = pNode->nToGoal + pNode->nCost;
            if (pNode->bClosed == 0)
            {
              sgAreaFinderWorkspace_g.heapUpdate(pNode, nKey);
            }
            else
            {
              sgAreaFinderWorkspace_g.heapInsert(pNode, nKey);
            }

            if (sgAreaFinderWorkspace_g.m_count == nQueueMax)
              break;
          }
        }
      }

      if (pSearch->nQueuePeak < sgAreaFinderWorkspace_g.m_count)
        pSearch->nQueuePeak = (short)sgAreaFinderWorkspace_g.m_count;

      if (sgAreaFinderWorkspace_g.m_count == nQueueMax || pFree == &aNodes[nNodesMax])
      {
        ++g_pathStats.nAreaCapped;  // Hyperion
        if (sgAreaFinderWorkspace_g.m_count != 0)
        {
          pFound = sgAreaFinderWorkspace_g.m_elems[1];
          sgAreaFinderWorkspace_g.m_elems[1] =
              sgAreaFinderWorkspace_g.m_elems[sgAreaFinderWorkspace_g.m_count];
          --sgAreaFinderWorkspace_g.m_count;
          sgAreaFinderWorkspace_g.heapifyDown(1);
        }
        break;
      }
    }

    if (pFound == NULL)
    {
      ++g_pathStats.nAreaShort;  // Hyperion
      pSearch->b01C = 1;
      pFound = &aNodes[0];
      aNodes[0].nToGoal = area_len(static_cast<ep_region_t *>(aNodes[0].pArea)->nX, nTargetX,
                                 static_cast<ep_region_t *>(aNodes[0].pArea)->nY, nTargetY);
      int nBest = aNodes[0].nToGoal;
      for (areauser_t *p = &aNodes[1]; p < pFree; ++p)
      {
        if (p->nToGoal < nBest)
        {
          pFound = p;
          nBest = p->nToGoal;
        }
      }
    }

    if (!bForward && pFound->nAreaId != nTo)
    {
      for (areauser_t *p = &aNodes[0]; p < pFree; ++p)
        region_user(static_cast<ep_region_t *>(p->pArea)) = NULL;
      nGoal = pFound->nAreaId;
      bRetry = 1;
      goto restart;
    }

    pSearch->nNodes = (short)(pFree - aNodes);
    int n;
    if (bForward)
    {
      n = 0;
      for (areauser_t *p = pFound; p != NULL; p = p->pPrev)
        ++n;
      int i = n - 1;
      while (i >= 50)
      {
        pFound = pFound->pPrev;
        --i;
      }
      for (; i >= 0; --i)
      {
        pSearch->nRegions[i] = pFound->nAreaId;
        pFound = pFound->pPrev;
      }
    }
    else
    {
      n = 0;
      for (; pFound != NULL; pFound = pFound->pPrev, ++n)
      {
        if (GetAreaIndex(pFound->nAreaId) != wStartArea)
          break;
        if (n < 50)
          pSearch->nRegions[n] = pFound->nAreaId;
      }
    }

    pSearch->bChainLen = (BYTE)n;
    pSearch->nRegionCount = (short)(n <= 50 ? n : 50);
    pSearch->nCursor = -1;
    for (areauser_t *p = &aNodes[0]; p < pFree; ++p)
      region_user(static_cast<ep_region_t *>(p->pArea)) = NULL;
    return n > 0;
  }
}
#endif  // OC_PAYLOAD

// Re-key one element already in the heap and move it back into place. The
// element is found by a linear scan from the end, and a key that rose sifts
// down while one that fell sifts up.
void PriorityQueue<areauser_t,125>::heapUpdate(areauser_t *pElem, int nKey)
{
  int nOld = pElem->nKey;
  if (nOld != nKey)
  {
    pElem->nKey = nKey;

    int i = m_count;
    while (i > 0 && m_elems[i] != pElem)
      --i;

    if (i != 0)
    {
      if (nOld > nKey)
        heapifyUp(i);
      else
        heapifyDown(i);
    }
  }
}

// Sift the element at `i` down until both children outrank it. 1.17 stores the
// saved element from three separate exits.
void PriorityQueue<areauser_t,125>::heapifyDown(int i)
{
  areauser_t *pElem = m_elems[i];

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

// Open an area search that already knows both endpoints differ: resolve the two
// regions, seat the current point and the unit's collision box, then hand over
// to sub_491C10.
BOOL sub_4919B0(path_search_t *pSearch)
{
  if (pSearch->ptStart.x == pSearch->ptGoal.x && pSearch->ptStart.y == pSearch->ptGoal.y)
    return 0;

  pSearch->nStartRegion = GetRegion_xy(pSearch->ptStart.x, pSearch->ptStart.y);
  pSearch->nGoalRegion = GetRegion_xy(pSearch->ptGoal.x, pSearch->ptGoal.y);

  pSearch->ptCur = pSearch->ptGoal;
  pSearch->b01C = 0;
  pSearch->b01D = 0;
  pSearch->b01E = 0;
  pSearch->bClosest = 0;

  pSearch->rcBound.left = (short)-gUnitBoundBox[pSearch->pUnit->wType].left;
  pSearch->rcBound.top = (short)-gUnitBoundBox[pSearch->pUnit->wType].top;
  pSearch->rcBound.right = gUnitBoundBox[pSearch->pUnit->wType].right;
  pSearch->rcBound.bottom = gUnitBoundBox[pSearch->pUnit->wType].bottom;

  pSearch->nNodes = 0;
  pSearch->n166 = 0;
  pSearch->nQueuePeak = 0;
  pSearch->n16A = 0;

  return sub_491C10(pSearch);
}
