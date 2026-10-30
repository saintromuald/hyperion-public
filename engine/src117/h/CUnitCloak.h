// CUnitCloak.h -- the one src117/lang/CUnitCloak.cpp declaration that
// src117/lang/net_slot.cpp reaches. Not a bid to open the rest of that TU.
//
// The signature is read off the oracle bytes. Free functions are spelled without an
// explicit __fastcall: /Gr already makes it the default, and a redundant one makes
// gen.py::find_decl read the name as ambiguous.

#ifndef OC117_CUNITCLOAK_H
#define OC117_CUNITCLOAK_H

#include "platform.h"

// 0x41D460 (96 B). ECX is read as 32 bits and EDX never, with no stack parameters, so one
// register argument; EAX is written on no path to a ret, so void.
void ShuffleColorSlots(int nArg0);

#endif  // OC117_CUNITCLOAK_H
