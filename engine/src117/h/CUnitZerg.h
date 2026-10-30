// CUnitZerg.h -- the free functions of src117/lang/CUnitZerg.cpp: zerg larva, burrow and
// creep. The class members are declared in CUnit.h with the rest of the class. Free
// functions are spelled without an explicit __fastcall, which /Gr already makes the
// default.

#ifndef OC117_CUNITZERG_H
#define OC117_CUNITZERG_H

#include "platform.h"

class CUnit;

// 0x43C490 sub_43C490 (196 B). Two register arguments and one stack word (`ret 4h`), each
// read as 32 bits. Returns int: twelve consuming callers use the full EAX and none narrows
// it.
int sub_43C490(int *pPos, int nArg1, int nArg2);

// 0x43D910 FindCountLarvae (45 B). A callback: its address is taken once and it has no
// direct call site, so the arity here is a lower bound and the indirect call site is the
// authority. ECX is a pointer to a class the bytes do not name; EDX is read as 32 bits.
// int, not the void the contract reads: every exit materialises EAX (`xor eax, eax`, and
// `sbb eax, eax / inc eax` for the count test), and the bounce search's filter slot
// consumes it as the stop flag.
int FindCountLarvae(void *pArg0, intptr_t nArg1);

#endif  // OC117_CUNITZERG_H
