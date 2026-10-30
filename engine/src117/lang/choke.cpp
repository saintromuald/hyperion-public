// choke.cpp -- the area-path fixup list: the nodes one region's path edit builds
// for its neighbours, and the pass that writes them back.
//
// Bodies are in ascending oracle address, 1.17's own order.
//
// The fixup list is a Storm TSList, so its terminator is a complemented address
// and not NULL -- read src117/h/TSList.h before touching any walk here. Every
// walk ends on a signed `p > 0`; written with `p != 0` it runs off the end.

#include "oc_data.h"  // Hyperion: OC_VA
#include "../h/choke.h"

#include <string.h>  // memcpy

#include "../h/Storm.h"            // SMemAlloc / SMemFree
#include "../h/rtti.h"             // type_info::raw_name -- OURS, not <typeinfo>
#include "../h/sai.h"              // GetRegionPosition
#include "../h/sai_PathCreate.h"   // ep_glob_t / ep_region_t
#include "../h/sai_TownBuilder.h"
#include "../h/sai_PathFindArea.h"   // path_search_t: sub_491300's one seat  // GetAreaIndex, sub_491300, TAiPathReq
#include "../gen/globals117.h"

// The globals this TU reaches, each spelled once for the standalone build and
// once for the payload.

// 0x69B584, the pathing glob, spelled here as src117/lang/CUnitPathUtil.cpp and
// src117/lang/CUnitPathBuild.cpp spell it.
#ifndef epGlob
#ifdef OC_DLL_MODE
#define epGlob (*reinterpret_cast<void **>(OC_VA(0x69B584u)))
#else
extern void *epGlob;
#endif
#endif

// 0x503030. The fixup list head: +0 is the route DWORD, +4 link.prev and +8
// link.next, the one realize_fixups starts every walk from and CreateNodes
// prepends to.
#ifndef OC_GAREAFIXUPS
#define OC_GAREAFIXUPS
#ifdef OC_DLL_MODE
#define gAreaFixups (*reinterpret_cast<TAREAFIXUPLIST *>(OC_VA(0x503030u)))
#else
extern TAREAFIXUPLIST gAreaFixups;
#endif
#endif

// The MSVC RTTI Type Descriptor at 0x510750: the type_info vftable 0x4F1EDC,
// a zero dword, then the raw name ".?AUTAreaFixup@@". CreateNodes and
// DeleteNode hand raw_name()'s result to SMemAlloc / SMemFree as the owner tag.
#ifndef OC_GTYPEINFO_TAREAFIXUP
#define OC_GTYPEINFO_TAREAFIXUP
#ifdef OC_DLL_MODE
#define gTypeInfo_TAreaFixup (*reinterpret_cast<type_info *>(OC_VA(0x510750u)))
#else
extern type_info gTypeInfo_TAreaFixup;
#endif
#endif

// 0x40E120. Unlink one node, free it, and return what followed it. The unlink is
// a static helper with the `nOff = 0` pre-assignment, the shape the CTRLNODE and
// _GAMENODE lists use; written inline it reassociates the offset.
static void UnlinkFixup(TAreaFixup *p)
{
  TAreaFixup *pprev = p->link.prev;
  if (pprev != 0) {
    TAreaFixup *pnext = p->link.next;
    TSLink<TAreaFixup> *pnl;
    if ((intptr_t)pnext <= 0) {
      pnl = reinterpret_cast<TSLink<TAreaFixup> *>(~(intptr_t)pnext);
    } else {
      intptr_t nOff = 0;
      if ((intptr_t)pnext > 0)
        nOff = (intptr_t)&p->link - (intptr_t)pprev->link.next;
      pnl = reinterpret_cast<TSLink<TAreaFixup> *>((intptr_t)pnext + nOff);
    }
    pnl->prev = pprev;
    p->link.prev->link.next = p->link.next;
    p->link.prev = 0;
    p->link.next = 0;
  }
}

__declspec(noinline) TAreaFixup *TAREAFIXUPLIST::DeleteNode(TAreaFixup *pNode)
{
  TSLink<TAreaFixup> *pl = pNode ? &pNode->link : &link;
  TAreaFixup *pRet = pl->next;
  if ((intptr_t)pRet <= 0)
    pRet = 0;
  UnlinkFixup(pNode);
  SMemFree(pNode, gTypeInfo_TAreaFixup.raw_name(), -2, 0);
  return pRet;
}

// 0x40E190. Try up to three chokepoints between two regions, building the
// fixup nodes for each in turn, and report which one was taken. The path
// request block is left uninitialised apart from the four fields set here.
// The two area ids sit in a local array: a plain WORD local is widened with
// `movzx esi, ax` where the oracle keeps `mov si, ax`.
int find_choke_regions(int nArg0, int nArg1, int nArg2, WORD *pwOut, int *pnOut,
               int nArg5)
{
  TAiPathReq req;

  nArg5 = (static_cast<unsigned int>(nArg5) > 2) ? nArg5 : 2;

  if (static_cast<WORD>(nArg0) == static_cast<WORD>(nArg1) ||
      static_cast<unsigned int>(nArg2) < static_cast<unsigned int>(nArg5)) {
    *pnOut = 4;
    return 0;
  }

  WORD awArea[2];
  awArea[0] = GetAreaIndex(static_cast<WORD>(nArg0));
  awArea[1] = GetAreaIndex(static_cast<WORD>(nArg1));
  if (awArea[0] != awArea[1]) {
    *pnOut = 2;
    return 0;
  }

  req.wFromRegion = static_cast<WORD>(nArg0);
  req.wToRegion = static_cast<WORD>(nArg1);
  // Hyperion: the points by field, positions of 4 bytes on x64 (stage E)
  point16_t ptFrom, ptTo;
  GetRegionPosition(static_cast<WORD>(nArg0), &ptFrom);
  GetRegionPosition(static_cast<WORD>(nArg1), &ptTo);
  req.nFromX = ptFrom.x;
  req.nFromY = ptFrom.y;
  req.nToX = ptTo.x;
  req.nToY = ptTo.y;

  int nRet = sub_40E4E0(&req, nArg2, pwOut, nArg5);
  int *pOut = pnOut;
  *pOut = nRet;
  if (nRet != 0)
    return 0;

  int nBefore = static_cast<WORD>(req.nRegionCount);
  unsigned int i = 1;
  WORD *pw = pwOut + 1;
  do {
    TAreaFixup::CreateNodes(pw[-1]);
    nRet = sub_40E4E0(&req, nArg2,
                      (i < 3) ? pw : reinterpret_cast<WORD *>(&pnOut), nArg5);
    *pOut = nRet;
    if (nRet != 0) {
      if (nRet == 3) {
        i = 0;
        goto done;
      }
      *pOut = 0;
      goto done;
    }
    i++;
    pw++;
  } while (i < 4);

  if (static_cast<short>(nBefore) + 10 < req.nRegionCount) {
    *pOut = 1;
    i = 3;
    goto done;
  }
  *pOut = 3;
  i = 0;

done:
  realize_fixups();
  return i;
}

// 0x40E2F0. Build one fixup node per neighbouring area, each holding the
// neighbour's path list with our own entry moved to the front, and remove that
// entry from the neighbour's list as it goes.
void TAreaFixup::CreateNodes(WORD wArea)
{
  ep_region_t *prSelf = &static_cast<ep_glob_t *>(epGlob)->aRegions[wArea];
  WORD *pPaths = prSelf->pPaths;
  int nLeft = prSelf->bPathCount;

  while (nLeft != 0) {
    nLeft--;
    ep_region_t *pn =
        &static_cast<ep_glob_t *>(epGlob)->aRegions[*pPaths];

    int nCount = pn->bPathCount;
    TAreaFixup *node = reinterpret_cast<TAreaFixup *>(SMemAlloc(
        nCount * 2 + sizeof(TAreaFixup), gTypeInfo_TAreaFixup.raw_name(), -2, 8));  // Hyperion: 0xC in 1.17
    TSLink<TAreaFixup> *nl;
    if (node != 0) {
      node->link.prev = 0;
      nl = &node->link;
      node->link.next = 0;
    } else {
      node = 0;
      nl = &gAreaFixups.link;
    }

    TAreaFixup *pPrev = nl->prev;
    if (pPrev != 0) {
      TAreaFixup *pn2 = nl->next;
      TSLink<TAreaFixup> *pnl;
      if ((intptr_t)pn2 <= 0)
        pnl = reinterpret_cast<TSLink<TAreaFixup> *>(~(intptr_t)pn2);
      else {
        // The unlink as the TU's other lists spell it; without the zero start and
        // the retest MSVC reassociates the offset into pn2's register.
        intptr_t nOff = 0;
        if ((intptr_t)pn2 > 0)
          nOff = (intptr_t)nl - (intptr_t)pPrev->link.next;
        pnl = reinterpret_cast<TSLink<TAreaFixup> *>((intptr_t)pn2 + nOff);
      }
      pnl->prev = pPrev;
      nl->prev->link.next = nl->next;
      nl->prev = 0;
      nl->next = 0;
    }

    nl->prev = reinterpret_cast<TAreaFixup *>(&gAreaFixups.link);
    nl->next = gAreaFixups.link.next;
    TAreaFixup *pFirst = gAreaFixups.link.next;
    TSLink<TAreaFixup> *pfl;
    intptr_t nOff = gAreaFixups._unmapped_00;
    if ((intptr_t)pFirst <= 0) {
      pfl = reinterpret_cast<TSLink<TAreaFixup> *>(~(intptr_t)pFirst);
    } else {
      if (nOff < 0)
        nOff = (intptr_t)&gAreaFixups.link - (intptr_t)gAreaFixups.link.prev->link.next;
      pfl = reinterpret_cast<TSLink<TAreaFixup> *>((intptr_t)pFirst + nOff);
    }
    pfl->prev = reinterpret_cast<TAreaFixup *>(nl);
    gAreaFixups.link.next = node;

    node->wArea = *pPaths;
    WORD *pDst = node->awPaths;
    WORD *pSrc = pn->pPaths;
    // Merged arms under nLeft != 0, which the loop already tests; they compile
    // to one load and give n EDX and pDst ESI, as in the oracle.
    int n; if (nLeft != 0) n = pn->bPathCount; else n = pn->bPathCount;
    while (n-- != 0 && wArea != *pSrc)
      *pDst++ = *pSrc++;

    *pDst++ = *pSrc;
    WORD *pFrom = pSrc + 1;
    for (; n != 0; n--) {
      *pSrc++ = *pFrom++;
      *pDst++ = *pSrc;
    }

    pn->bPathCount--;
    pn->bPathFirst--;
    pPaths++;
  }
}

// 0x40E480. Write every pending fixup back into its region's path list and free
// the node. The walk ends on a signed `p > 0` because the terminator is a
// complemented address.
void realize_fixups()
{
  TAreaFixup *p = gAreaFixups.link.next;

  while ((intptr_t)p > 0) {
    ep_region_t *pr = &static_cast<ep_glob_t *>(epGlob)->aRegions[p->wArea];
    pr->bPathCount++;
    pr->bPathFirst++;
    memcpy(pr->pPaths, p->awPaths, 2 * pr->bPathCount);
    p = gAreaFixups.DeleteNode(p);
  }
}

// 0x40E4E0. Pick the highest-priority region in one window of the path the
// request produced. The window is clamped from both ends and the result is 0
// when one was found, 3 when none was and 2 when the request itself failed.
// The window's top comes from a separate local; mutating nCount in place gives
// `sub eax, 2h` where the oracle has `add eax, -2h`.
int sub_40E4E0(TAiPathReq *pReq, int nLast, WORD *pwOut, int nFirst)
{
  if (sub_491300(reinterpret_cast<path_search_t *>(pReq)) == 0 ||
      *reinterpret_cast<const BYTE *>(&pReq->dwUnknown01C) != 0)
    return 2;

  int nCount = pReq->nRegionCount;
  if (static_cast<unsigned int>(nCount) < static_cast<unsigned int>(nFirst))
    return 4;

  int nMax = nCount - 2;
  if (static_cast<unsigned int>(nMax) < static_cast<unsigned int>(nLast))
    nLast = nMax;
  if (static_cast<unsigned int>(nLast) < static_cast<unsigned int>(nFirst))
    return 4;

  DWORD dwBest = 0;
  WORD *p = &pReq->aRegions[nFirst];
  if (static_cast<unsigned int>(nFirst) <= static_cast<unsigned int>(nLast)) {
    int n = nLast - nFirst + 1;
    do {
      DWORD dw = GetDefPriority(*p);
      if (dw != 0 && dw >= dwBest) {
        dwBest = dw;
        *pwOut = *p;
      }
      p++;
    } while (--n != 0);
  }
  return dwBest != 0 ? 0 : 3;
}
