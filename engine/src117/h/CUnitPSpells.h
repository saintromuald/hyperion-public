// CUnitPSpells.h -- the free functions of src117/lang/CUnitPSpells.cpp: the protoss unit
// spells, stasis, paralyze, hallucination and psi feedback. The class members are declared
// in CUnit.h with the rest of the class. Free functions are spelled without an explicit
// __fastcall, which /Gr already makes the default.

#ifndef OC117_CUNITPSPELLS_H
#define OC117_CUNITPSPELLS_H

#include "platform.h"

class CUnit;
class CThingy;

// 0x430B00 HallucinateHangar (144 B). ECX read as 32 bits, one stack word (`ret 4h`) that
// is dereferenced, and a second register argument the callee never reads: it exists, since
// /Gr fills EDX before the body touches the stack, but the body shows neither its width
// nor its type, so the int and the void * are placeholders. Spelled void: EAX at the ret
// is inherited rather than set and no caller reads it.
void HallucinateHangar(intptr_t nArg0, int nArg1, void *pArg2);

// 0x430B90 sub_430B90 (63 B). Returns the CThingy it allocated, AllocThingy's result left
// in ESI; both callers discard it. ECX is forwarded to a callee untouched, so this body
// shows no width for it at all; EDX is read as 32 bits.
CThingy *sub_430B90(int nArg0, intptr_t nArg1);

// 0x430ED0 Hallucinate (142 B). ECX read as 32 bits, one stack word (`ret 4h`), and a
// second register argument the callee ignores, as in HallucinateHangar. Returns int: three
// callers use the full EAX and none narrows it. The stack word is read both wide and
// narrow, which cannot separate int from a narrower spelling -- MSVC loads the whole dword
// slot even for a BYTE or WORD parameter and narrows afterwards.
intptr_t Hallucinate(intptr_t nArg0, int nArg1, int nArg2);

#endif  // OC117_CUNITPSPELLS_H
