// CUnitPathCollide.h -- the one free function of src117/lang/CUnitPathCollide.cpp, the
// unit-to-unit collision queries the path state machine calls. The class members are
// declared in CUnit.h with the rest of the class. Free functions are spelled without an
// explicit __fastcall, which /Gr already makes the default.

#ifndef OC117_CUNITPATHCOLLIDE_H
#define OC117_CUNITPATHCOLLIDE_H

#include "platform.h"

class CUnit;


// 0x42CB90 (223 B), `ret 8h`, both register arguments read as 32 bits. Two
// consuming callers use the full EAX.
int sub_42CB90(intptr_t nArg0, int nArg1, intptr_t nArg2, int nArg3);

#endif  // OC117_CUNITPATHCOLLIDE_H
