// find.h -- declarations for src117/lang/find.cpp: the free-function unit-proximity
// searches. Class members are declared in CUnit.h and its search headers.

#ifndef OC117_FIND_H
#define OC117_FIND_H

#include "platform.h"
#include "rect16.h"   // the search area both proximity rows take
#include "pos.h"      // Hyperion: spos_t, mrect_t

class CUnit;          // declared, not included: taken by pointer only

// The predicate both proximity searches take.
typedef BOOL(__fastcall *find_filter_t)(CUnit *pCandidate, void *pContext);

// The coordinates are 16-bit: sub_442D90 reads both with `movsx`, and the
// harvester callers load them as dwords off a point16_t. The radius is a WORD,
// the only spelling sub_442CC0 reproduces.
extern "C" CUnit *__fastcall sub_442CC0(spos_t x, spos_t y, WORD nRadius,
                                        find_filter_t pfnFilter, void *pContext);
// C linkage lets CUnitSearches.cpp call it through its own view, an int radius
// and CUnit * filters, on the same @sub_442CC0@20.
CUnit *__fastcall sub_442CC0(spos_t x, spos_t y, WORD nRadius, find_filter_t pfnFilter,
                             void *pContext);
CUnit *__fastcall sub_442D90(CUnit *pExclude, spos_t nPosX, spos_t nPosY,
                             int xLo, int yLo, int xHi, int yHi,
                             const mrect_t *pArea, find_filter_t pfnFilter,
                             void *pContext);

#endif  // OC117_FIND_H
