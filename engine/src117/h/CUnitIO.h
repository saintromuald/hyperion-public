// CUnitIO.h -- declarations for src117/lang/CUnitIO.cpp: unit id hashing and save-pointer fixup
//
// Class members belong in the class header; this file owns the free functions. gen.py
// emits a hook row only for a name it can find in a header, so a free function declared
// only in the .cpp is rolled back at gen time. Spell free functions without an explicit
// __fastcall: /Gr already makes it the default and a redundant one makes find_decl read
// the name as ambiguous, after which it emits nothing.
//
// Every signature here is read off the oracle before it is written: the ret N, which of
// ECX/EDX is a real input, each stack slot's width and sign, and what the callers do
// with EAX.

#ifndef OC117_CUNITIO_H
#define OC117_CUNITIO_H

#include "platform.h"

class CUnit;

// 0x427570 FixupAIPointer (91 B). Bare ret, ECX read as a 32-bit input and EDX never, no
// stack parameters, so one register argument. Void: EAX is defined on some paths to the
// ret and not others, and where it is defined it is whatever bldg_id / guard_id /
// worker_id left rather than an explicit value, with no caller reading it. If a caller
// elsewhere does consume the result this is an int, and the byte comparison cannot tell
// the two apart.
void FixupAIPointer(intptr_t nArg0);


#endif  // OC117_CUNITIO_H
