// CUnitUpdate2.h -- the header of src117/lang/CUnitUpdate2.cpp, the per-frame
// movement and update primitives. The TU's functions are CUnit members, declared
// in CUnit.h, so it declares nothing of its own.
//
// The primitives have their own TU, apart from CUnitUpdate.cpp, because that
// file's bodies call them and /Ob2 would inline a callee defined beside them;
// 1.17's bytes call them, so its source kept them apart too.
#pragma once

#include "platform.h"   // ours: BYTE/WORD + <stddef.h> for offsetof

class CUnit;
class CSprite;