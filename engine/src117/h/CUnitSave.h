// CUnitSave.h -- the free functions of src117/lang/CUnitSave.cpp, the unit table
// save. The free functions are __fastcall through /Gr and are declared without
// the keyword.

#ifndef OC117_CUNITSAVE_H
#define OC117_CUNITSAVE_H

#include "platform.h"

class CUnit;

// 0x431280. ECX the per-slot byte map, EDX its length; marks every unit on the
// four lists, and a listed unit's subunit.
void sub_431280(BYTE *pUsed, int nCount);

// 0x431340. ECX nonzero to encode, EDX the byte map, its length on the stack.
// Returns the number of marked slots; the one caller discards it and
// WriteSave's inlined copy keeps it.
int saveload_unitlist(int nMode, BYTE *pUsed, int nCount);

// 0x4313C0.
BOOL sub_4313C0();

// 0x4313D0. ECX the unit, EDX the file; returns pfwrite's result.
BOOL write_unit_id(CUnit *pUnit, void *pFile);

#endif  // OC117_CUNITSAVE_H
