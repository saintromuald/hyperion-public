// CUnitMisc.h -- the free functions of src117/lang/CUnitMisc.cpp; the TU's CUnit
// members are declared in CUnit.h with the rest of the class.

#ifndef OC_CUNITMISC_H
#define OC_CUNITMISC_H

#include "platform.h"   // ours: BYTE/WORD + the fixed-width scaffold

// 0x428CE0. The unit type is 16 bits, in CX: the bound check is `cmp cx, ax`
// (unsigned) and the table index a movzx of CX. Returns a C string, the pooled
// empty string at 0x4EC14E when the type is out of range. CUnit::GetName
// (0x428D10) inlines it, and the standalone copy remains for other callers.
const char *__fastcall get_unit_name_from_type(WORD wUnitType);

class CUnit;

// 0x429110. Not CUnit::IsEnemyUnit (0x4290E0), which asks whether another unit
// is hostile to `this`: this one asks on behalf of a player, ECX, about the unit
// in EDX. Both ask about a unit owned by player 11 through the player who
// created its sprite, CSprite::bCreator at +0x0A.
BOOL __fastcall IsEnemyUnit(int nPlayer, CUnit *pUnit);

// 0x429320. ECX is a unit type, compared at 16 bits against three constants;
// nothing is dereferenced.
BOOL __fastcall IsBuiltRefineryType(WORD wUnitType);

// 0x429500 / 0x4294D0 / 0x429490: entries of one callback table, __stdcall with
// four dword stack words, each returning 1. Every word is a full dword even
// where only a byte is used, and the bodies narrow it. The first word is a
// CUnit * in sub_429500 and unused in the other two.
BOOL __stdcall sub_429500(CUnit *pUnit, int a2, int a3, int a4);
BOOL __stdcall increment_upgrade_cb(int a1, int nUpgradeId, int nLevel, int nPlayer);
BOOL __stdcall research_tech_cb(int a1, int nTechId, int nEnable, int nPlayer);

#endif  // OC_CUNITMISC_H
