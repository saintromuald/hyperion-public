// sai_PathFindUtil.h -- declarations for src117/lang/sai_PathFindUtil.cpp: the
// closest-reachable search and the bin table the area search uses to mask
// blocked directions. The free functions are __fastcall through /Gr and are
// declared without the keyword.

#ifndef OC117_SAI_PATHFINDUTIL_H
#define OC117_SAI_PATHFINDUTIL_H

#include "platform.h"
#include "point16.h"
#include "rect16.h"
#include "sai_PathFindArea.h"  // path_search_t

// 0x492A90. ECX is the path_search_t, read at +0x18 as a point and at +0x1F as
// a flag; EDX is stored and read back as a dword. The three callers hold a
// differently spelled view of the same 0x16C-byte block and cast it.
BOOL GetClosestReachable(path_search_t *pSearch, int nRegion);

// 0x492C00. ECX is the same path_search_t, read at +0x158 and +0x15C; EDX is a
// point16_t, read and written.
BOOL sub_492C00(path_search_t *pSearch, point16_t *pPt);

// The bin table sub_492DE0 builds and sub_4930B0 reads is defined in the TU.

// 0x493200. Resets the bins; the value left in EAX is the map width on its way
// into gBinEdges_g[1], and the one caller ignores it.
void ResetBins(void);

// 0x4930B0. ECX is a point read as signed shorts; EDX is a byte the body ANDs
// in place.
void sub_4930B0(point16_t *pPt, BYTE *pMask);

// 0x492DE0. ECX is a rect16_t read as signed shorts.
BOOL sub_492DE0(rect16_t *pRect);

#endif  // OC117_SAI_PATHFINDUTIL_H
