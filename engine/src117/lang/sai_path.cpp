// sai_path.cpp -- the AI path object: the 1024-node pool (25,600 in a game past 1.17's limits,
// revision 15) and its free list, the per-unit path timer, and the step/follow entry points
// CUnit uses. Bodies are in oracle VA order.
//
// A callee defined in this file can be spliced into a caller in this file by
// /Ob2, which the oracle (WPO off) never does across TUs; the lever against it
// is __declspec(noinline) on the declaration.

#include "oc_data.h"  // Hyperion: OC_VA
#include <string.h>                   // memset / memcpy
#include "../h/sai_path.h"
#include "../h/CUnit.h"
#include "../h/CSprite.h"
#include "../h/CTarget.h"             // @veclen@16
#include "../h/CUnitCombat.h"         // UnitCanReachGroundTarget_xy
#include "../h/CUnitPathBuild.h"      // sub_42C840
#include "../h/SAI_Banker.h"          // GetRegion_xy
#include "../h/sai.h"                // GetRegionPosition
#include "../h/sai_PathFindArea.h"   // path_search_t + sub_491210 / sub_4919B0 / sub_491300
#include "../h/Storm.h"               // SMemAlloc / SMemFree
#include "../gen/globals117.h"
#include "../h/poollimits.h"            // Hyperion: g_pools.paths
#include <vector>

// Globals this TU reaches, dual-spelled, with one #ifdef per name rather than
// one over the block: a single guard would undeclare every name under it.

// The free-list head and the pool base. Both names are the sheet's own spelling
// for this TU (sc-refs/sheets/data_naming_1.17.csv, sai_path.cpp rows).
#ifdef OC_DLL_MODE
#define sgFreePath      (*reinterpret_cast<sai_path **>(OC_VA(0x69B570u)))
#else
extern sai_path *sgFreePath;
#endif
#ifdef OC_DLL_MODE
#define sai_PathPoolBuf (*reinterpret_cast<sai_path **>(OC_VA(0x69B574u)))
#else
extern sai_path *sai_PathPoolBuf;
#endif

// The game frame counter, already spelled this way in gameloop.cpp.
#ifdef OC_DLL_MODE
#define dword_52651C    (*reinterpret_cast<DWORD *>(OC_VA(0x52651Cu)))
#else
extern DWORD dword_52651C;
#endif

// The bodies, in oracle VA order.

// Report every pool node that is not on the free list. The reporting callee is
// 0x4BE950, the one-byte `ret` /OPT:ICF folded every empty function onto; gen
// thunks this one-argument spelling.
void __fastcall voice_squelch(void *pPath);

// The next link is read before the mark is stored, and the report loop indexes
// the pool so the offset is the lea's base register.
void sub_48E600(int nMode)
{
  sai_path *p = sgFreePath;
  // Hyperion: the game's paths, 1,024 in 1.17 (revision 15)
  static std::vector<int> s_marks;
  s_marks.assign(g_pools.paths, 0);
  int *aMark = s_marks.data();

  while (p != NULL)
  {
    int nNext = p->nNextFree;
    aMark[p->dwWaypoint - 1] = 1;
    if (nNext == 0)
      p = NULL;
    else
      p = &sai_PathPoolBuf[nNext - 1];
  }

  for (int i = 0; i < g_pools.paths; i++)
  {
    if (aMark[i] == 0)
      voice_squelch(&sai_PathPoolBuf[i]);
  }
}

// Pop the head of the free list. The argument is the byte size the caller needs
// (sub_42C840 computes it and tail-jumps here); anything over one node fails.
sai_path *sai_path_new(int nBytes)
{
  if (nBytes > 0x80)
    return NULL;

  sai_path *pNode = sgFreePath;
  if (pNode == NULL)
    return NULL;

  int nNext = pNode->nNextFree;
  if (nNext == 0)
    sgFreePath = reinterpret_cast<sai_path *>((intptr_t)nNext);
  else
    sgFreePath = &sai_PathPoolBuf[nNext - 1];
  return pNode;
}

// Push a node back onto the free list. Both links are stored as one-based node
// indexes, so each one costs a pointer difference.
void sai_path_recycle(sai_path *pPath)
{
  pPath->dwWaypoint = (DWORD)((pPath - sai_PathPoolBuf) + 1);

  sai_path *pHead = sgFreePath;
  if (pHead == NULL)
    pPath->nNextFree = (int)reinterpret_cast<intptr_t>(pHead);
  else
    pPath->nNextFree = (int)((pHead - sai_PathPoolBuf) + 1);
  sgFreePath = pPath;
}

// Tick the unit's path timer down; TRUE once it has run out.
BOOL sai_path_progress_timer(CUnit *pUnit)
{
  sai_path *pPath = pUnit->pPath;
  if (pPath != NULL)
  {
    if (pPath->b19 == 0)
      return TRUE;
    pPath->b19 = static_cast<BYTE>(pPath->b19 - 1);
  }
  return FALSE;
}

void sai_path_set_timer(CUnit *pUnit, int nTicks)
{
  sai_path *pPath = pUnit->pPath;
  if (pPath != NULL)
    pPath->b19 = static_cast<BYTE>(nTicks);
}

// Give the unit's path node back and drop it. sai_path_recycle is spliced in.
void sai_path_kill(CUnit *pUnit)
{
  sai_path *pPath = pUnit->pPath;
  if (pPath != NULL)
  {
    sai_path_recycle(pPath);
    pUnit->pPath = NULL;
  }
}

// Release the node pool.
void sai_pathpool_free()
{
  if (sai_PathPoolBuf != NULL)
  {
    SMemFree(sai_PathPoolBuf, "Sai_path.cpp", 792, 0);
    sai_PathPoolBuf = NULL;
  }
}

// Allocate the 1024-node pool and thread every node onto the free list.
// The next link is stored before the waypoint, which puts the counter copy
// ahead of the compare; sgFreePath is set from p so the base is not reloaded.
// Hyperion: the paths the pool was made with (revision 15, SaiPathPoolForGame)
static int s_nPathsMade;

void sai_pathpool_init()
{
  // Hyperion: the game's paths, 1,024 (0x20000 bytes) in 1.17 (revision 15)
  s_nPathsMade = g_pools.paths;
  const size_t cb = static_cast<size_t>(g_pools.paths) * sizeof(sai_path);
  sai_PathPoolBuf = reinterpret_cast<sai_path *>(
      SMemAlloc(static_cast<unsigned>(cb), "Sai_path.cpp", 769, 0));
  memset(sai_PathPoolBuf, 0, cb);

  sai_path *p = sai_PathPoolBuf;
  sgFreePath = p;
  int i = 1;
  do
  {
    p->nNextFree = i + 1;
    p->dwWaypoint = i;
    i++;
    ++p;
  } while (i <= g_pools.paths);
  p[-1].nNextFree = 0;
}

// Hyperion: the pool made again for the game's paths once game_init has sized its pools
// (revision 15): sai_path_init makes it before, with the last game's.
void SaiPathPoolForGame()
{
  if (sai_PathPoolBuf != NULL && s_nPathsMade == g_pools.paths)
    return;
  sai_pathpool_free();
  sai_pathpool_init();
}

// veclen through int parameters. The oracle inlines it into two callers here;
// the parameters make all four arguments load before either push.
static int hop_length(int nAx, int nBx, int nAy, int nBy)
{
  return veclen(nAx, nBx, nAy, nBy);
}

// Ground distance from the unit to (x, y): the straight line when no pathing is
// needed, 7FFFh when the target is unreachable, 7FFEh when the search fails,
// and otherwise the sum of the hops between the region centres it returned.
int sai_path_compute_distance(CUnit *pUnit, spos_t x, spos_t y)
{
  path_search_t req;
  point16_t ptPrev;
  point16_t ptCur;

  if (!(pUnit->bPathingFlags & 1))
  {
    CSprite *pSp = pUnit->pSprite;
    upos_t wY = pSp->wPosY;
    upos_t wX = pSp->wPosX;
    return veclen(PosS(wX), x, PosS(wY), y);
  }

  if (!UnitCanReachGroundTarget_xy(pUnit, x, y))
    return 0x7FFF;

  CSprite *pSprite = pUnit->pSprite;
  req.ptStart.x = pSprite->wPosX;
  req.ptStart.y = pSprite->wPosY;
  req.ptGoal.x = x;
  req.ptGoal.y = y;
  req.pUnit = pUnit;
  req.ptCur = req.ptGoal;
  *reinterpret_cast<DWORD *>(&req.b01C) = 0;
  req.nStartRegion = GetRegion_xy(req.ptStart.x, req.ptStart.y);
  req.nGoalRegion = GetRegion_xy(req.ptGoal.x, req.ptGoal.y);

  if (req.nStartRegion == req.nGoalRegion)
  {
    req.nCursor = -1;
    req.nRegionCount = 1;
    req.bChainLen = 1;
    req.nRegions[0] = req.nStartRegion;

    CSprite *pSp2 = pUnit->pSprite;
    upos_t wY2 = pSp2->wPosY;
    upos_t wX2 = pSp2->wPosX;
    return veclen(PosS(wX2), req.ptGoal.x,
                  PosS(wY2), req.ptGoal.y);
  }

  if (!sub_491300(&req))
    return 0x7FFE;

  if (req.nRegionCount <= 2)
    return sai_path_distance_to_dest(pUnit, &req);

  int nTotal = 0;
  GetRegionPosition(req.nRegions[0], &ptPrev);
  for (int i = 1; i < req.nRegionCount; ++i)
  {
    GetRegionPosition(req.nRegions[i], &ptCur);
    nTotal += hop_length(ptPrev.x, ptCur.x, ptPrev.y, ptCur.y);
    ptPrev = ptCur;
  }
  return nTotal;
}

// TRUE unless the unit still has pathing flag 1, in which case the terrain
// reachability test decides. The tail call reuses the incoming y slot.
//
// The two ends disagree about the sign of a 16-bit slot and both are right.
// Callers zero-extend (CUnitSearches.h's WORD); this body re-reads it signed
// for UnitCanReachGroundTarget_xy's int parameters, and declaring it short
// would narrow each caller's load and cost those matches.
BOOL __fastcall sai_path_can_reach_target(CUnit *pUnit, upos_t x, upos_t y)
{
  if (!(pUnit->bPathingFlags & 1))
    return TRUE;
  return UnitCanReachGroundTarget_xy(pUnit, PosS(x),
                                     PosS(y));
}

// Straight-line distance from the unit's sprite to the request's destination.
int sai_path_distance_to_dest(CUnit *pUnit, path_search_t *pReq)
{
  CSprite *pSprite = pUnit->pSprite;
  spos_t y = pSprite->wPosY;
  spos_t x = pSprite->wPosX;
  return veclen(x, pReq->ptGoal.x, y, pReq->ptGoal.y);
}

// TRUE while the unit should keep walking its current path leg: it has not
// arrived, it is not already aimed at the move target, and it is neither close
// enough nor far enough off-angle to want a new one.
// The sprite X is not named for the first compare: named, it stays live
// beside the CSE'd path X and the third push lands before the early exits.
// veclen goes through hop_length so its four arguments load before the pushes.
BOOL CUnit::sub_48EE40(int nUnused)
{
  sai_path *pPathNode = pPath;
  if (pPathNode == NULL)
    return FALSE;

  CSprite *pSp = pSprite;
  if (pSp->wPosX == pPathNode->awWaypoint[0] &&
      PosU(pSp->wPosY) == pPathNode->awWaypoint[1])
    return FALSE;

  if (pPathNode->dwWaypoint == PosPack(wMoveTargetX, wMoveTargetY))  // Hyperion: by field (stage E)
    return TRUE;

  if (_unmapped_24[3] == 0)
  {
    upos_t wY = pSp->wPosY;
    upos_t wX = pSp->wPosX;
    int nDist = hop_length(PosS(wX),
                           static_cast<short>(pPathNode->awWaypoint[0]),
                           PosS(wY),
                           pPathNode->ptWaypoint.y);
    if (nDist <= 16 &&
        nDist * 2 < static_cast<BYTE>(get_angle(bDirection, _unmapped_2C[0x4B - 0x2C])))
    {
      if (pPathNode->c1F < pPathNode->cRegionCount - 1)
        return FALSE;

      const WORD *pRun =
          reinterpret_cast<const WORD *>(&pPathNode->adwRegions[pPathNode->cRegionCount]);
      CSprite *pSpr = pSprite;
      upos_t wRy = pSpr->wPosY;
      upos_t wRx = pSpr->wPosX;
      if (GetRegion_xy(PosS(wRx), PosS(wRy)) ==
          pRun[pPathNode->cStepIndex])
        return FALSE;
    }
  }
  return TRUE;
}
// Drop the unit's path and open a fresh one-step path to the packed target.
// sai_path_kill and sai_path_new are both spliced in; the size request is a
// constant, so new's over-size guard folds away.
void CUnit::PathOneStep(point16_t ptTarget, point16_t ptArg1)
{
  if (pPath != NULL)
    sai_path_recycle(pPath);
  sai_path *pNew = sai_path_new(0x28);
  pPath = pNew;
  if (pNew != NULL)
  {
    *reinterpret_cast<DWORD *>(&pNew->cStepCount) = 0x10001;
    pNew->b1B = 1;
    pNew->ptStart.x = pSprite->wPosX;
    pNew->ptStart.y = pSprite->wPosY;
    pNew->ptWaypoint = ptTarget;
    pNew->pt08 = ptArg1;
    pNew->bFlags = 0;
    pNew->b19 = 0;
    *reinterpret_cast<point16_t *>(&pNew->adwRegions[0]) = ptTarget;
  }
}

// Walk one leg of the unit's path. If it is already on course nothing happens;
// otherwise either the next waypoint is taken directly or a new sub-target is
// searched for, and only a failed search with no path left reports FALSE.
int CUnit::PathFollow(mpoint_t ptTarget)
{
  point16_t ptNext;

  // Hyperion: the target goes packed, as the path node keeps it (stage F)
  if (!sub_48EE40(static_cast<int>(PosPack(ptTarget.x, ptTarget.y))))
  {
    if (!(dwFlags & 0x100000))
    {
      SetMoveTarget_xy(ptTarget.x, ptTarget.y);
      PathOneStep(ptTarget, ptTarget);
      return TRUE;
    }

    if (!sai_path_waypoint(this, &ptNext, ptTarget) && pPath == NULL)
      return FALSE;

    SetSubTarget(ptNext.x, ptNext.y);
    sub_48EE40(static_cast<int>(PosPack(ptTarget.x, ptTarget.y)));
  }
  return TRUE;
}

// Region of the last hop of a path between the two points, or the
// destination's own region when there is no path to walk.
WORD CUnit::CreatePath(point16_t ptFrom, point16_t ptTo)
{
  path_search_t req;

  req.ptStart = ptFrom;
  req.ptGoal = ptTo;
  req.nStartRegion = GetRegion_xy(req.ptStart.x, req.ptStart.y);
  req.nGoalRegion = GetRegion_xy(req.ptGoal.x, req.ptGoal.y);

  if (req.nStartRegion == req.nGoalRegion)
    return req.nGoalRegion;
  req.pUnit = this;
  req.ptCur = req.ptGoal;
  *reinterpret_cast<DWORD *>(&req.b01C) = 0;
  if (!sub_491300(&req))
    return req.nGoalRegion;
  return req.nRegions[req.nRegionCount - 1];
}
// Build a path node from a filled request block. The node's two runs share its
// 96-byte tail, so the WORD run starts where the region run ends and the step
// index is clamped to the number of steps that fit.
sai_path *sai_unit_path_from_info(path_search_t *pReq)
{
  sai_path *p = reinterpret_cast<sai_path *>(sub_42C840(
      &pReq->nRegionCount,
      reinterpret_cast<short *>(&pReq->nPathCount)));
  if (p == NULL)
    return NULL;

  p->b19 = 0;
  p->dwFrame = dword_52651C;
  p->bFlags = 0;
  p->cRegionCount = *reinterpret_cast<BYTE *>(&pReq->nPathCount);
  p->c1F = 0;
  p->cStepCount = static_cast<BYTE>(pReq->nRegionCount);
  p->b1B = pReq->bChainLen;
  p->cStepIndex = static_cast<char>(pReq->nCursor);
  if (p->cStepIndex > p->cStepCount)
    p->cStepIndex = p->cStepCount;

  memcpy(&p->adwRegions[0], &pReq->adwPath[0], p->cRegionCount * 4);
  memcpy(reinterpret_cast<DWORD *>(p) + (p->cRegionCount + 8),
          &pReq->nRegions[0], p->cStepCount * 2);
  return p;
}

// The inverse of sai_unit_path_from_info: fill a request block from the unit's
// sprite and the node it is walking.
void sai_path_from_unit_path(CUnit *pUnit, sai_path *pPath, path_search_t *pReq)
{
  pReq->pUnit = pUnit;
  pReq->ptStart.x = pUnit->pSprite->wPosX;
  pReq->ptStart.y = pUnit->pSprite->wPosY;
  pReq->ptGoal = *reinterpret_cast<point16_t *>(&pPath->dw08);

  memcpy(&pReq->adwPath[0], &pPath->adwRegions[0],
          pPath->cRegionCount * 4);
  *reinterpret_cast<short *>(&pReq->nPathCount) =
      pPath->cRegionCount;
  *reinterpret_cast<short *>(&pReq->nPathIndex) = pPath->c1F;

  memcpy(&pReq->nRegions[0],
          reinterpret_cast<DWORD *>(pPath) + (pPath->cRegionCount + 8),
          pPath->cStepCount * 2);
  pReq->nRegionCount = pPath->cStepCount;
  pReq->nCursor = pPath->cStepIndex;
  pReq->bChainLen = pPath->b1B;
}

// Bit 19 of the unit's flag word. The oracle stores it with MSVC's bitfield
// insert (`xor eax, [mem]` / `and` / `xor [mem], eax`), which only a bitfield
// assignment keeps in memory on both sides.
struct unit_flags_bits {
  DWORD _00 : 19;
  DWORD b19 : 1;
  DWORD _20 : 12;
};

// The length of a path leg. The oracle inlines it: the two pairs arrive as
// whole dwords and are split by shifts, which named DWORD locals do not give.
static int path_leg_length(point16_t ptA, point16_t ptB)
{
  return veclen(ptA.x, ptB.x, ptA.y, ptB.y);
}

// Take the next waypoint on the unit's path, re-running the area search when
// the current path has run out or does not exist, and report whether a waypoint
// came back. The 0x10 flag is dropped once the waypoint IS the move target, and
// the 0x40 flag is set when the leg is under 16 away.
// The search block returns FALSE itself; a shared `fail:` exit moves the
// first sprite load after the node is built from EAX to ECX.
BOOL sai_path_waypoint(CUnit *pUnit, point16_t *pOut, point16_t ptTarget)
{
  path_search_t req;

  BYTE bFlags = pUnit->bMoveFlags;
  sai_path *p = pUnit->pPath;
  pUnit->bMoveFlags = static_cast<BYTE>((bFlags & ~0x40) | 0x10);

  if (p != NULL)
  {
    ++p->c1F;
    if (p->c1F >= p->cRegionCount)
    {
      sai_path_from_unit_path(pUnit, p, &req);
      sai_path_kill(pUnit);

      int nCursor = req.nCursor;
      int nCount = req.nRegionCount;
      if (nCursor + 1 < nCount &&
          (req.bChainLen == nCount || nCursor + 4 < nCount))
      {
        // The search result is tested twice and the count check is skipped
        // when it failed; a plain three-term && loses the shape.
        BOOL bOk = sub_4919B0(&req);
        if ((!bOk || req.nRegionCount != 0) &&
            *reinterpret_cast<short *>(&req.nPathCount) != 0 && bOk)
          goto have_path;
      }
    }
  }

  if (pUnit->pPath == NULL)
  {
    req.pUnit = pUnit;
    req.ptStart.x = pUnit->pSprite->wPosX;
    req.ptStart.y = pUnit->pSprite->wPosY;
    req.ptGoal = ptTarget;
    if (!sub_491210(&req) || req.nRegionCount == 0 ||
        *reinterpret_cast<short *>(&req.nPathCount) == 0)
      return FALSE;

have_path:
    if (req.b01E != 0 &&
        (req.ptCur.x != ptTarget.x ||
         req.ptCur.y != ptTarget.y))
    {
      DWORD dwCur = PosPack(req.ptCur.x, req.ptCur.y);  // Hyperion: packed by value (stage E)
      *reinterpret_cast<DWORD *>(&ptTarget) = dwCur;
      pUnit->sub_420DB0(req.ptCur.x, static_cast<short>(dwCur >> 16));
      reinterpret_cast<unit_flags_bits *>(&pUnit->dwFlags)->b19 = req.bClosest;
    }

    pUnit->pPath = sai_unit_path_from_info(&req);
    p = pUnit->pPath;
    if (p == NULL)
      return FALSE;

    p->ptStart.x = pUnit->pSprite->wPosX;
    p->ptStart.y = pUnit->pSprite->wPosY;
    p->pt08.x = ptTarget.x;
    p->pt08.y = ptTarget.y;
  }

  DWORD dwWaypoint = p->adwRegions[p->c1F];
  p->dwWaypoint = dwWaypoint;
  *reinterpret_cast<DWORD *>(pOut) = dwWaypoint;

  if (p->dwWaypoint == PosPack(pUnit->wMoveTargetX, pUnit->wMoveTargetY))  // Hyperion: by field (stage E)
  {
    pUnit->bMoveFlags &= ~0x10;
    return TRUE;
  }

  if (path_leg_length(p->ptWaypoint, p->pt08) < 0x10)
    pUnit->bMoveFlags |= 0x40;
  return TRUE;
}

