// count.h -- the supply and unit counting functions of oracle count.cpp that
// other TUs call; not the TU's whole interface.
#pragma once

#include "platform.h"

// 0x414360 (86 B) and 0x414320 (50 B). Race in CL, player in DL, both BYTE: the
// callers move bytes (CUnit::GetRace's AL, CUnit::bPlayer) with no movzx. The
// return is a full DWORD, a 32-bit table load; some callers narrow it after.
int count_get_supplies_available(BYTE bRace, BYTE bPlayer);
int count_get_supplies_used(BYTE bRace, BYTE bPlayer);
// 0x414550 (36 B). The unit type arrives in CX and indexes gubUnitCountInfo
// (0x523C40). An int, not a WORD: the callers widen the WORD field with movzx
// before the call. The return is a BYTE, meaningful in AL only. C linkage, which
// gamesnd.cpp's own declaration of it also has; the thunk generator reads the
// plain form below.
extern "C" BYTE count_race(int nUnitType);
BYTE count_race(int nUnitType);

class CUnit;

// 0x414060 (14 B) takes two register arguments; 0x414070 (12 B) and 0x414590
// (10 B) take one and tail-jump to count_unit_exists, so void. Reached by the
// unit-creation and morph paths.
void count_add_completion(CUnit *pUnit, int nPlayer);
void count_add_existence(CUnit *pUnit);
void count_remove_existence(CUnit *pUnit);


// 0x4142E0 (57 B). banker.cpp's bldg_check_food calls it.
unsigned __fastcall count_food_max(WORD nUnitType, BYTE bPlayer);

// The two workers the six add/remove wrappers forward to. Each takes the delta
// in ECX (+1 to add, -1 to remove) and the unit in EDX; count_unit_complete
// closes `ret 4h`, so its third argument is the one stack word.
void count_unit_complete(int nDelta, CUnit *pUnit, int nFlag);
void count_unit_exists(int nDelta, CUnit *pUnit);

