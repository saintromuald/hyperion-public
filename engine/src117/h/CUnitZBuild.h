// CUnitZBuild.h -- the free functions of src117/lang/CUnitZBuild.cpp: the zerg drone build
// and structure placement. The class members are declared in CUnit.h with the rest of the
// class. Free functions are spelled without an explicit __fastcall, which /Gr already
// makes the default.

#ifndef OC117_CUNITZBUILD_H
#define OC117_CUNITZBUILD_H

#include "platform.h"

class CUnit;

// 0x43B380 sub_43B380 (382 B). One register argument, in ECX, read as 32 bits. Spelled
// void: EAX at the ret is whatever sub_4B46E0 left there and no caller reads it. If one
// elsewhere does, it is an int, and the bytes cannot tell the two apart.
void sub_43B380(intptr_t nArg0);

// 0x43B580 ClipToGameScreen (93 B). One register argument, in ECX, a pointer to a class the
// bytes do not name. BOOL: every ret materialises 0 or 1 and its one caller uses the full
// EAX.
BOOL ClipToGameScreen(void *pArg0);

// 0x43C320 type_is_morphed_structure (49 B). One register argument, in ECX, read as 32
// bits. BOOL: every ret materialises 0 or 1.
BOOL type_is_morphed_structure(int nArg0);

#endif  // OC117_CUNITZBUILD_H
