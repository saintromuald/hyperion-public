// sai_PathFindArea.h -- declarations for src117/lang/sai_PathFindArea.cpp: the
// area-level A* the AI runs before the local tile search. The free functions are
// __fastcall through /Gr and are declared without the keyword.

#ifndef OC117_SAI_PATHFINDAREA_H
#define OC117_SAI_PATHFINDAREA_H

#include "platform.h"
#include "point16.h"
#include "rect16.h"
#include "pos.h"      // Hyperion: mpoint_t, mrect_t (stage E)

#include "PriorityQueue.h"
#include "maplimits.h"  // Hyperion: the searches' budgets

class CUnit;

// One search node, 0x18 apart in the 350-entry array sub_491300 puts on its own
// stack. bClosed is 1 while the node sits outside the queue and 0 once it is
// queued again; nCost is the distance walked so far and nKey that plus the
// straight-line rest, which is what the heap orders on.
struct areauser_t {
  /*0x00*/ BYTE        bClosed;
  /*0x01*/ BYTE        _01;
  /*0x02*/ WORD        nAreaId;
  /*0x04*/ void       *pArea;
  /*0x08*/ areauser_t *pPrev;
  /*0x0C*/ int         nToGoal;
  /*0x10*/ int         nCost;
  /*0x14*/ int         nKey;
};

// The heap this TU instantiates: element pointers from slot 1, the count after
// them, and a sentinel parked in slot 0 so the sift-up needs no index test.
// Hyperion: the 64-bit build's holds a larger map's budget, g_epAreaQueue
// being the limit a search keeps to.
#if defined(_M_IX86)
#define AREA_QUEUE_CAP 125
#else
#define AREA_QUEUE_CAP EP_AREA_QUEUE_MAX
#endif
template <> struct PriorityQueue<areauser_t,125> {
  /*0x000*/ areauser_t *m_elems[AREA_QUEUE_CAP + 1];
  /*0x1F8*/ int         m_count;
  /*0x1FC*/ areauser_t *m_pSentinel;

  void heapUpdate(areauser_t *pElem, int nKey);
  void heapifyDown(int i);
  void heapifyUp(int i);
  void heapInsert(areauser_t *pElem, int nKey);
};

OC_X86_LAYOUT(sizeof(areauser_t) == 0x18, "areauser_t stride");
static_assert(offsetof(areauser_t, bClosed) == 0x00, "areauser_t.bClosed");
static_assert(offsetof(areauser_t, nAreaId) == 0x02, "areauser_t.nAreaId");
OC_X86_LAYOUT(offsetof(areauser_t, pArea) == 0x04, "areauser_t.pArea");
OC_X86_LAYOUT(offsetof(areauser_t, pPrev) == 0x08, "areauser_t.pPrev");
OC_X86_LAYOUT(offsetof(areauser_t, nToGoal) == 0x0C, "areauser_t.nToGoal");
OC_X86_LAYOUT(offsetof(areauser_t, nCost) == 0x10, "areauser_t.nCost");
OC_X86_LAYOUT(offsetof(areauser_t, nKey) == 0x14, "areauser_t.nKey");

// offsetof is a macro, so the comma in the argument list needs a name.
typedef PriorityQueue<areauser_t,125> areaqueue_t;

OC_X86_LAYOUT(sizeof(areaqueue_t) == 0x200, "areaqueue_t size");
static_assert(offsetof(areaqueue_t, m_elems) == 0x000, "areaqueue_t.m_elems");
OC_X86_LAYOUT(offsetof(areaqueue_t, m_count) == 0x1F8, "areaqueue_t.m_count");
OC_X86_LAYOUT(offsetof(areaqueue_t, m_pSentinel) == 0x1FC,
              "areaqueue_t.m_pSentinel");

// The request the area search works on. Built by its caller, handed to
// sub_4919B0 or sub_491210 in ECX, and left holding the region chain the local
// search then walks. rcBound is the unit's own collision box with the two near
// edges negated. Everything between +0x88 and +0x153 is untouched by this TU.
struct path_search_t {
  /*0x000*/ CUnit    *pUnit;
  // sai_get_ground_distance writes each point as two shorts.
  // Hyperion: the points and boxes are of 4-byte positions on x64 (h/pos.h, stage E)
  /*0x004*/ mpoint_t  ptStart;
  /*0x008*/ mpoint_t  ptGoal;
  // The unit that blocked the walk and the box it was blocked in, which
  // sub_491C10, the local search, stores.
  /*0x00C*/ CUnit    *pBlocker;
  /*0x010*/ mrect_t   rcBlock;
  // Starts as ptGoal, copied as one dword, and advances as the search walks.
  /*0x018*/ mpoint_t  ptCur;
  /*0x01C*/ BYTE      b01C;
  /*0x01D*/ BYTE      b01D;
  /*0x01E*/ BYTE      b01E;
  /*0x01F*/ BYTE      bClosest;
  /*0x020*/ short     nRegions[50];
  /*0x084*/ short     nCursor;
  /*0x086*/ short     nRegionCount;
  // The region chain the search produces, plus its cursor and length.
  // sub_492860 walks adwPath and writes the two WORDs at its tail.
  /*0x088*/ DWORD     adwPath[50];
  /*0x150*/ WORD      nPathIndex;
  /*0x152*/ WORD      nPathCount;
  /*0x154*/ BYTE      bChainLen;
  /*0x155*/ BYTE      _155[3];
  /*0x158*/ mrect_t   rcBound;
  /*0x160*/ WORD      nStartRegion;
  /*0x162*/ WORD      nGoalRegion;
  /*0x164*/ short     nNodes;
  /*0x166*/ short     n166;
  /*0x168*/ short     nQueuePeak;
  /*0x16A*/ short     n16A;
};

OC_X86_LAYOUT(sizeof(path_search_t) == 0x16C, "path_search_t size");
static_assert(offsetof(path_search_t, pUnit) == 0x000, "path_search_t.pUnit");
OC_X86_LAYOUT(offsetof(path_search_t, ptStart) == 0x004, "path_search_t.ptStart");
OC_X86_LAYOUT(offsetof(path_search_t, ptGoal) == 0x008, "path_search_t.ptGoal");
OC_X86_LAYOUT(offsetof(path_search_t, ptCur) == 0x018, "path_search_t.ptCur");
OC_X86_LAYOUT(offsetof(path_search_t, pBlocker) == 0x00C, "path_search_t.pBlocker");
OC_X86_LAYOUT(offsetof(path_search_t, rcBlock) == 0x010, "path_search_t.rcBlock");
OC_X86_LAYOUT(offsetof(path_search_t, adwPath) == 0x088, "path_search_t.adwPath");
OC_X86_LAYOUT(offsetof(path_search_t, nPathIndex) == 0x150, "path_search_t.nPathIndex");
OC_X86_LAYOUT(offsetof(path_search_t, nPathCount) == 0x152, "path_search_t.nPathCount");
OC_X86_LAYOUT(offsetof(path_search_t, b01C) == 0x01C, "path_search_t.b01C");
OC_X86_LAYOUT(offsetof(path_search_t, b01D) == 0x01D, "path_search_t.b01D");
OC_X86_LAYOUT(offsetof(path_search_t, b01E) == 0x01E, "path_search_t.b01E");
OC_X86_LAYOUT(offsetof(path_search_t, bClosest) == 0x01F, "path_search_t.bClosest");
OC_X86_LAYOUT(offsetof(path_search_t, nRegions) == 0x020, "path_search_t.nRegions");
OC_X86_LAYOUT(offsetof(path_search_t, nCursor) == 0x084, "path_search_t.nCursor");
OC_X86_LAYOUT(offsetof(path_search_t, nRegionCount) == 0x086,
              "path_search_t.nRegionCount");
OC_X86_LAYOUT(offsetof(path_search_t, bChainLen) == 0x154, "path_search_t.bChainLen");
OC_X86_LAYOUT(offsetof(path_search_t, rcBound) == 0x158, "path_search_t.rcBound");
OC_X86_LAYOUT(offsetof(path_search_t, nStartRegion) == 0x160,
              "path_search_t.nStartRegion");
OC_X86_LAYOUT(offsetof(path_search_t, nGoalRegion) == 0x162,
              "path_search_t.nGoalRegion");
OC_X86_LAYOUT(offsetof(path_search_t, nNodes) == 0x164, "path_search_t.nNodes");
OC_X86_LAYOUT(offsetof(path_search_t, n166) == 0x166, "path_search_t.n166");
OC_X86_LAYOUT(offsetof(path_search_t, n16A) == 0x16A, "path_search_t.n16A");
OC_X86_LAYOUT(offsetof(path_search_t, nQueuePeak) == 0x168,
              "path_search_t.nQueuePeak");

// 0x491210 and 0x4919B0, ECX in and their one caller reading 32-bit EAX; both
// tail-jump into sub_491C10, which decides the return.
BOOL sub_491210(path_search_t *pSearch);
BOOL sub_4919B0(path_search_t *pSearch);

// 0x491300. Nine callers, all reading 32-bit EAX.
BOOL sub_491300(path_search_t *pSearch);

#endif  // OC117_SAI_PATHFINDAREA_H
