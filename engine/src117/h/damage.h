// damage.h -- declarations for src117/lang/damage.cpp, the weapon damage
// scaling. The free functions are __fastcall through /Gr and are declared
// without the keyword.

#ifndef OC117_DAMAGE_H
#define OC117_DAMAGE_H

#include "platform.h"

// 0x43EB20. ECX the damage, DL the damage type, the unit size in a stack byte.
int compute_damage_from_types(int nDamage, BYTE bDamageType, BYTE bUnitSize);

#endif  // OC117_DAMAGE_H
