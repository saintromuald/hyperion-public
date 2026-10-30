// CUnitCarrier.h -- the free functions of src117/lang/CUnitCarrier.cpp, the Carrier and
// Reaver hangar family. The member functions are declared in CUnit.h with the rest of the
// class, sub_41BFE0 among them: this TU calls it twice, and it is a CUnit member although
// the corpus names it free. Free functions are spelled without an explicit __fastcall,
// which /Gr already makes the default. The point header is included rather than
// forward-declared because sub_41D100 takes a point by value.

#ifndef OC117_CUNITCARRIER_H
#define OC117_CUNITCARRIER_H

#include "platform.h"
#include "pos.h"         // ours: sub_41D100's by-value third parameter, a map point

class CUnit;

// sub_41D100 -- 0x41D100 (189 B). Scatters a point by a random per-axis jitter, mirrors the
// jitter back inward at the map edge, clips, and sets the unit's destination to the result.
// All three callers are CUnit::DispatchStrafeUnit. One stack dword (`ret 4h`), read as two
// 16-bit halves, written in place and passed on to ClipToMap by address: a point by value.
// The scale is a BYTE -- `movzx esi, dl` -- and an int would lose the movzx.
void sub_41D100(CUnit *pUnit, BYTE bScale, mpoint_t pt);

#endif  // OC117_CUNITCARRIER_H
