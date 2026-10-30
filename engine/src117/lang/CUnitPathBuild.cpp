// CUnitPathBuild.cpp -- starting a unit's path: sizing a path node, the two path starts,
// the destination resolve behind them, and CUnit::KillPath. Bodies are in oracle VA order.
//
// A callee defined in this file can be spliced into a caller here by /Ob2, while the
// oracle, built with WPO off, cannot splice across a TU boundary: a callee left in another
// TU reproduces that for free, and __declspec(noinline) is the lever when one must live
// here.

#include "oc_data.h"  // Hyperion: OC_VA
#include <stddef.h>          // offsetof for rect16.h / point16.h

#include "../h/CUnitPathBuild.h"
#include "../h/CUnit.h"
#include "../h/CSprite.h"
#include "../h/CUnitPathUtil.h"
#include "../h/rect16.h"
#include "../h/point16.h"
#include "../h/sai_path.h"
#include "../h/sai_Contours.h"
#include "../h/sai_PathCreate.h"
#include "../h/sai_PathFindUtil.h"
#include "../h/sai.h"
#include "../h/sai_TownBuilder.h"
#include "../gen/globals117.h"

// The globals this TU reaches, dual-spelled: one #ifdef per name, never one over the whole
// block, since the generated set grows over time and a single guard would go false and
// undeclare every name under it.

#ifdef OC_DLL_MODE
#ifndef epGlob
#define epGlob (*reinterpret_cast<void **>(OC_VA(0x69B584u)))
#endif
#ifndef gUnitBoundBox
#define gUnitBoundBox (reinterpret_cast<rect16_t *>(OC_VA(0x522C38u)))
#endif
#else
extern rect16_t gUnitBoundBox[228] OC_UNALIGNED;        // 0x522C38
extern void *epGlob;                       // 0x69B584
#endif

// The scratch block GetClosestReachable fills is sai_PathFindArea.h's path_search_t: the
// same 0x16C size and offsets, with names. It uses ptCur at +0x18, read and written as one
// packed dword, and rcBound at +0x158. The frame is 0x170 because the oracle's
// `sub esp, 170h` also covers the 4-byte region local at [ebp-4].
OC_X86_LAYOUT(sizeof(path_search_t)             == 0x16C, "sub_42C9A0: sub esp, 170h less the [ebp-4] local");
OC_X86_LAYOUT(offsetof(path_search_t, ptCur)    == 0x018, "sub_42C9A0: mov dword ptr [ebp-158h], eax");
OC_X86_LAYOUT(offsetof(path_search_t, rcBound)  == 0x158, "sub_42C9A0: mov word ptr [ebp-18h], ax");

// 0x42C840 (96 B). Sizes a path node from two counts and hands the allocation off. The
// node is 0x20 + 2a + 4b rounded up to a dword; anything over 0x80 is clamped, and both
// clamp arms land back on exactly 0x80. The tail call is what pins the return.
void *sub_42C840(short *pnA, short *pArg1)
{
    WORD *pwB = reinterpret_cast<WORD *>(pArg1);

    int nSize = ((*pnA + *pwB * 2) * 2 + 0x23) & 0xFFFC;
    if (nSize > 0x80) {
        if (static_cast<short>(*pwB) >= 0x18) {
            *pwB = 0x17;
            *pnA = 1;
        } else {
            *pnA = static_cast<short>((0x18 - *pwB) * 2);
        }
        nSize = ((*pnA + static_cast<short>(*pwB) * 2) * 2 + 0x23) & 0xFFFC;
    }
    return sai_path_new(nSize);
}

// 0x42C8A0 (30 B).
void CUnit::KillPath()
{
    if (pPath) {
        sai_path_recycle(pPath);
        pPath = NULL;
    }
}

// 0x42C8C0 (108 B). Starts a path toward pPos, boxed by the unit type's bound box and
// anchored at the sprite's own pixel. The packed point goes out in the dead parameter
// slot, which is where MSVC homes the local.
BOOL CUnit::sub_42C8C0(point16_t *pPos)
{
    if (sub_42ED50(wType, pPos->x, pPos->y))
        return FALSE;

    rect16_t box = gUnitBoundBox[wType];
    mpoint_t  pt;
    pt.x = pSprite->wPosX;
    pt.y = pSprite->wPosY;
    // Hyperion: the start goes packed, as 1.17's path node keeps it (stage F)
    return sub_42C9A0(reinterpret_cast<intptr_t>(pPos), reinterpret_cast<intptr_t>(&box),
                      static_cast<int>(PosPack(pt.x, pt.y)));
}

// 0x42C930 (103 B). The same start, but the box is built by halving the two extents the
// caller hands in; each half is read back out of the field it was just stored to.
BOOL sub_42C930(intptr_t nArg0, int nArg1, int nArg2, int nArg3)
{
    point16_t *pPos = reinterpret_cast<point16_t *>(nArg0);

    if (sub_42EC70(pPos->x, pPos->y, nArg1, nArg3))
        return FALSE;

    rect16_t rcExtent;  // a unit's extents, not a position: sub_42C9A0 reads it as WORDs
    rcExtent.top = static_cast<short>(nArg3 / 2);
    rcExtent.bottom = static_cast<short>(nArg3 - rcExtent.top);
    rcExtent.left = static_cast<short>(nArg1 / 2);
    rcExtent.right = static_cast<short>(nArg1 - rcExtent.left);
    return sub_42C9A0(nArg0, reinterpret_cast<intptr_t>(&rcExtent), nArg2);
}

// 0x42C9A0 (222 B). Resolves a destination pixel to a walkable one and hands the search
// on. The region pair is only consulted when the destination is land and in a different
// area; otherwise CreatePath picks the region, called on a null object.
BOOL sub_42C9A0(intptr_t nArg0, intptr_t nArg1, int nArg2)
{
    point16_t *pPos = reinterpret_cast<point16_t *>(nArg0);
    const WORD *pBox = reinterpret_cast<const WORD *>(nArg1);
    path_search_t ws;
    int nRegion;

    BOOL bLand = is_land_pixel(pPos->x, pPos->y);
    static_cast<ep_glob_t *>(epGlob)->pContours->SetSearch(
        reinterpret_cast<const rect16_t *>(nArg1));
    if (bLand) {
        nRegion = GetRegionFromPx(pPos->x, pPos->y);
        // Both halves are read as dwords and narrowed, the high one from the
        // slot's own address plus two; `nArg2 >> 16` enregisters it and sars.
        const int *pnLow = &nArg2;
        const int *pnHigh = reinterpret_cast<const int *>(
            reinterpret_cast<const BYTE *>(pnLow) + 2);
        if (GetAreaIndex(static_cast<WORD>(nRegion)) ==
            GetAreaIndex(GetRegionFromPx(static_cast<short>(*pnLow),
                                         static_cast<short>(*pnHigh))))
            goto search;
    }
    nRegion = reinterpret_cast<CUnit *>(0)->CreatePath(
        *reinterpret_cast<const point16_t *>(&nArg2), *pPos);
search:
    ws.ptCur = *pPos;  // Hyperion: the point, not its DWORD (stage E)
    GetClosestReachable(&ws, nRegion);
    *pPos = ws.ptCur;
    ws.rcBound.top = static_cast<short>(-static_cast<int>(pBox[1]));
    ws.rcBound.bottom = static_cast<short>(pBox[3]);
    ws.rcBound.left = static_cast<short>(-static_cast<int>(pBox[0]));
    ws.rcBound.right = static_cast<short>(pBox[2]);
    sub_492C00(&ws, pPos);
    return TRUE;
}
