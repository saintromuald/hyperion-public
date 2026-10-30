// CUnitOrder.h -- declarations for src117/lang/CUnitOrder.cpp, the unit order
// queue. Its CUnit members are declared in CUnit.h; this file has the one free
// function.
#pragma once

#include "platform.h"

class CUnit;

// 0x429C80 (58 B). Whether pUnit's race builds wUnitType as its refinery;
// callers pass `this` in ECX and an unwidened unit type in EDX.
BOOL __fastcall unit_race_matches_refinery_bldg(CUnit *pUnit, WORD wUnitType);
