// damage.cpp -- the weapon damage scaling by damage type and unit size.
//
// Bodies are in ascending oracle address, 1.17's own order.

#include "oc_data.h"  // Hyperion: OC_VA
#include "../h/damage.h"
#include "../gen/globals117.h"

// The globals this TU reaches, each spelled once for the standalone build and
// once for the payload.

// The percent table, a header row and column around damage type x unit size.
#ifdef OC_DLL_MODE
#define sgnPercent (reinterpret_cast<DWORD (*)[5]>(OC_VA(0x503228u)))
#else
extern DWORD sgnPercent[6][5] OC_UNALIGNED;
#endif

// Scales a damage amount by the damage type's percent against a unit size.
int compute_damage_from_types(int nDamage, BYTE bDamageType, BYTE bUnitSize) {
  return (sgnPercent[bDamageType + 1][bUnitSize + 1] * nDamage) >> 8;
}
