// CUnitCOP.h -- the free functions of src117/lang/CUnitCOP.cpp: the circle of power and the
// CTF flag handlers. The class members are declared in CUnit.h with the rest of the class.
// Free functions are spelled without an explicit __fastcall, which /Gr already makes the
// default.

#ifndef OC117_CUNITCOP_H
#define OC117_CUNITCOP_H

#include "platform.h"

class CUnit;

// 0x4215A0 FlagCaptured (96 B). A callback: its address is taken once and nothing calls it
// directly, so the arity here is a lower bound -- a register it ignores does not show in
// the bytes -- and the indirect call site is the authority. ECX is read as 32 bits, EDX as
// a pointer to a class the bytes do not name, and every path to the ret sets EAX to zero.
BOOL FlagCaptured(intptr_t nArg0, void *pArg1);

// 0x421650 ctf_flag_captured (134 B). One register argument, in ECX. Spelled void: EAX at
// the ret is inherited rather than set and no caller reads it. If one elsewhere does, it is
// an int, and the bytes cannot tell the two apart.
void ctf_flag_captured(int nArg0);

// 0x4216E0 ctf_flag_returned (113 B). One argument in ECX, EAX at the ret inherited and
// unread, as above.
void ctf_flag_returned(int nArg0);

// 0x421760 ctf_flag_taken (134 B). One argument in ECX, EAX at the ret inherited and
// unread, as above.
void ctf_flag_taken(int nArg0);

#endif  // OC117_CUNITCOP_H
