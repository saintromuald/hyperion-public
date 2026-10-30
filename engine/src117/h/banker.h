// banker.h -- declarations for src117/lang/banker.cpp, the resource and cost
// accessors. Member functions are in their class headers (CUnit.h and others).
// This file has four of the TU's free functions; the other ten are declared in
// SAI_Banker.h. Free functions are __fastcall through /Gr and are declared
// without the keyword.

#ifndef OC117_BANKER_H
#define OC117_BANKER_H

#include "platform.h"

// 0x408460 (37 B). ECX is the player (8 bits), EDX the unit type (16 bits); no
// caller reads EAX.
void bldg_add_resources(BYTE bPlayer, WORD wUnitType);

// 0x408490 (63 B). The same shape as bldg_add_resources; no caller reads EAX.
void refund_unit_partial(BYTE bPlayer, WORD wUnitType);

// 0x4084D0 (268 B). ECX 8 bits, EDX 32 bits, and one stack word (`ret 4h`). Every
// return is 0 or 1 and three callers read the full EAX, so BOOL. EDX stays int:
// the body reads all 32 bits.
BOOL bldg_check_food(BYTE bPlayer, int nArg1, int nArg2);

// 0x4085E0 (105 B). ECX 8 bits, EDX 32 bits, no stack arguments. All three
// callers discard the result, but every path sets an explicit value in EAX,
// which a void body would not emit, so int.
int bldg_check_resources(BYTE bPlayer, int nArg1);

#endif  // OC117_BANKER_H
