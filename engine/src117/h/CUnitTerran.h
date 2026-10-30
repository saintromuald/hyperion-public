// CUnitTerran.h -- the two callbacks of src117/lang/CUnitTerran.cpp and the free callees in
// other TUs its bodies reach. The CUnit members are declared in CUnit.h with the rest of
// the class.

#ifndef OC117_CUNITTERRAN_H
#define OC117_CUNITTERRAN_H

#include "platform.h"
#include "rect16.h"
#include "pos.h"      // Hyperion: mrect_t

class CUnit;

typedef int (__fastcall *CUnitTerranFindCb)(CUnit *pUnit, CUnit *pContext);

// find_foreach_unit's oracle symbol at 0x442B60. ECX/EDX carry the area and
// callback, the context is one stack slot, and the callback returns nonzero to
// stop the walk.
CUnit *__fastcall sub_442B60(const mrect_t *pArea,
                             CUnitTerranFindCb pfnCallback,
                             CUnit *pContext);
#if defined(_WIN64) || defined(__LP64__)
// Hyperion: 1.17's box, for the callers that still take it.
inline CUnit *sub_442B60(const rect16_t *pRect, CUnitTerranFindCb pfnCallback, CUnit *pContext) {
  mrect_t rc = *pRect;
  return sub_442B60(&rc, pfnCallback, pContext);
}
#endif

// Callback bodies defined in CUnitTerran.cpp. Both take (unit, context) in the
// fastcall register pair and return a full-width stop flag in EAX.
int __fastcall DestroyBurrowed(CUnit *pUnit, CUnit *pBuilding);
int __fastcall sub_436F20(CUnit *pUnit, CUnit *pContext);

// Search callback, the same shape as sub_436F20 above. It has no direct caller at all:
// CUnit::BuildingCrushTank passes its address.
int __fastcall crush_tank_cb(CUnit *pUnit, CUnit *pContext);

void __fastcall status_set_update();
void __fastcall gamesnd_do_liftoff(CUnit *pUnit);
// BOOL, not void: the callee materialises EAX on every exit (0x4B47F0's own BOOL exits,
// and sub_4B4A10's tail-jmp forwards its caller's test). The one landed call site in
// CUnitTerran.cpp ignores the result.
BOOL __fastcall sub_4B47F0(CUnit *pUnit);
void __fastcall sub_47D530();
int __fastcall placebox_is_on();

#endif  // OC117_CUNITTERRAN_H
