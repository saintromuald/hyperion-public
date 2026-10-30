// CUnitZSpells.h -- what src117/lang/CUnitZSpells.cpp needs beyond CUnit.h: the thingy
// header, the two find-callback types and the free helpers of the zerg spell and status
// block. The member functions are declared in CUnit.h with the rest of the class.

#ifndef OC117_CUNITZSPELLS_H
#define OC117_CUNITZSPELLS_H

#include "platform.h"
#include "point16.h"
#include "rect16.h"
#include "pos.h"     // Hyperion: mpoint_t, mrect_t

class CUnit;

#include "CThingy.h"
#include "collide.h"   // mtx_passable (the broodling placement test)

typedef int (__fastcall *CUnitZSpellsFindProc)(CUnit *pCandidate,
                                               CUnit *pContext);
typedef int (__fastcall *CUnitZSpellsPlayerProc)(CUnit *pCandidate,
                                                 int nPlayer);

void __fastcall count_add_killed(CUnit *pUnit, int nPlayer);
// Free fastcall: the broodling caller supplies pUnit/pInput in ECX/EDX, then
// pOutput, pBounds and two zero flags on the stack; the callee closes ret 10h.
BOOL __fastcall FindPlaceForUnit(CUnit *pUnit, mpoint_t *pInput,
                                 mpoint_t *pOutput, mrect_t *pBounds,
                                 int a4, int a5);
#if defined(_WIN64) || defined(__LP64__)
// Hyperion: 1.17's point and box, for the callers that still take them. The
// search clamps the box it is given, so the box goes back too.
inline BOOL FindPlaceForUnit(CUnit *pUnit, point16_t *pInput, point16_t *pOutput,
                             rect16_t *pBounds, int a4, int a5) {
  mpoint_t ptIn = *pInput, ptOut = *pOutput;
  mrect_t rc;
  if (pBounds != NULL)
    rc = *pBounds;
  BOOL bFound = FindPlaceForUnit(pUnit, &ptIn, &ptOut, pBounds != NULL ? &rc : NULL, a4, a5);
  *pOutput = ptOut;
  if (pBounds != NULL)
    *pBounds = rc;
  return bFound;
}
#endif
void __fastcall register_unit_for_ai(CUnit *pUnit, CUnit *pParent);

#endif  // OC117_CUNITZSPELLS_H
