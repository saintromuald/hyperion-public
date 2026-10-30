// CUnitInit.h -- the free functions of src117/lang/CUnitInit.cpp; the TU's CUnit
// members are declared in CUnit.h. The free functions are __fastcall through /Gr
// and are declared without the keyword. It includes only typelimits.h, for order_id_t.
#pragma once

#include "typelimits.h"  // Hyperion: order_id_t

// 0x425610. For each units.dat entry, raises its target-acquisition range to at
// least the maximum range of the weapons that fire for it, in tiles (pixels >> 5).
// Through the turret subunit it reads and writes at different indices; see the
// body.
void cunit_init_findrange();

// 0x4265D0. Returns 0 and reads none of its three stack words. It is the
// creep-support callback CUnit::sub_425270 hands remove_creep_completely, so it
// has creep_box_cb_t's type.
struct rect32_t;
BOOL __stdcall sub_4265D0(int nX, int nY, rect32_t *pBox);

// 0x4266D0.
void sub_4266D0();

// 0x426700. ECX is a byte map indexed per unit slot. The caller loads EDX with
// the map's length, which the body never reads.
void sub_426700(BYTE *pUsed, int nCount);

// 0x424D30. The unit type is 16 bits, in CX; x is in EDX and y on the stack.
BOOL sub_424D30(int nUnitType, int x, int y);

// 0x424860. Type in ECX and x in EDX, then y, the owner and a BYTE on the stack;
// returns the new unit.
CUnit *sub_424860(int nUnitType, int x, int y, int nPlayer, BYTE bRand);

// 0x426000. The 0x144-byte record sibling of sub_426130.
BOOL sub_426000(void *pFile, DWORD nCount);

// 0x425DE0. The save file in ECX and the save version in DX.
BOOL read_save_units(void *pFile, WORD wVersion);

// 0x426130. ECX is the save file handed to decompress, EDX the record count.
BOOL sub_426130(void *pFile, DWORD nCount);

// 0x4261F0. Type in ECX (compared as a word), x in EDX, y and the owner on the
// stack; returns the new unit. C linkage, so TrigAct.cpp can declare the same
// @trigger_create_unit@16 with a WORD type; the plain redeclaration after it
// keeps that linkage.
extern "C" CUnit *trigger_create_unit(int nUnitType, int x, int y, int nPlayer);
CUnit *trigger_create_unit(int nUnitType, int x, int y, int nPlayer);

// 0x4265E0. The order id is one byte, and all six callers read AX, so the
// return is a WORD.
WORD get_order_tech_energy_cost(order_id_t bOrderId);

// 0x424900. Type in ECX, x in EDX, then y and the owner on the stack; returns
// the new unit or NULL.
CUnit *__fastcall CreateUnitAt(int nUnitType, int x, int y, int nPlayer);
