// CUnitPath.h -- the header of src117/lang/CUnitPath.cpp, the unit path family.
// Its functions are CUnit members, declared in CUnit.h, so this file declares
// nothing. sai_path is defined in CUnitPath.cpp; its kill callee is declared in
// sai_path.h.
//
// The movement primitives the path bodies call, PathAtMoveTarget and PathFollow
// among them, stay in their own TUs: /Ob2 would splice them in from this one.
#pragma once

#include "platform.h"   // BYTE/WORD and <stddef.h> for offsetof

class CUnit;
class CSprite;
