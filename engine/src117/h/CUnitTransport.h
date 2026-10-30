// CUnitTransport.h -- the free functions of src117/lang/CUnitTransport.cpp, the transport
// and bunker cargo layer. The member functions are declared in their class header,
// src117/h/CUnit.h and friends. Free functions are spelled without an explicit __fastcall,
// which /Gr already makes the default.

#ifndef OC117_CUNITTRANSPORT_H
#define OC117_CUNITTRANSPORT_H

#include "platform.h"
#include "point16.h"
#include "pos.h"      // Hyperion: mpoint_t

class CUnit;

// 0x438180 (75 B). ECX and EDX are both live-in and the stack carries one more word
// (`ret 4h`): ECX is a CUnit, dereferenced at +0Ch and then at +14h/+16h of that, the
// CUnit -> CSprite -> (wPosX, wPosY) chain; EDX becomes the `this` of
// CUnit::SetUnitPosition and of FindPlaceForUnit; the stack word goes straight through as
// FindPlaceForUnit's own third argument. Returns int: the tail normalises that function's
// result to 0 or 1 with `neg eax / sbb eax, eax / neg eax` and its one caller reads all 32.
int sub_438180(CUnit *pAnchor, CUnit *pUnit, mpoint_t *pOutput);  // Hyperion: a map point

#endif  // OC117_CUNITTRANSPORT_H
