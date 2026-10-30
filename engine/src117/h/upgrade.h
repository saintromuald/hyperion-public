// upgrade.h -- declarations for src117/lang/upgrade.cpp, 0x4BBCB0 to
// 0x4BBDCB: the two enumerators that walk a unit's tech and upgrade tables and
// hand each entry to a caller-supplied callback.
//
// Both read no register input and pop twelve bytes: __stdcall.

#ifndef OC117_UPGRADE_H
#define OC117_UPGRADE_H

#include "platform.h"

class CUnit;

// The enumerator's callback: callee-cleaned, four stack dwords, and the walk
// stops when it returns zero. The arguments are the unit, the table entry's id,
// what the accessor answered for it, and the caller's own third argument passed
// straight through.
typedef int(__stdcall *PFN_TECH_ENUM)(CUnit *pUnit, int nId, int nState, int nCtx);

// Walks the five tech entries for the unit's type and calls back for each,
// with get_tech_researched's answer as the third argument. Rejects a null unit
// or a null callback with SErrSetLastError(57h) and returns without calling.
void __stdcall sub_4BBCB0(CUnit *pUnit, PFN_TECH_ENUM pfnEnum, int nCtx);

// The upgrade twin of sub_4BBCB0, differing only in the accessor it consults.
void __stdcall sub_4BBD40(CUnit *pUnit, PFN_TECH_ENUM pfnEnum, int nCtx);

#endif  // OC117_UPGRADE_H
