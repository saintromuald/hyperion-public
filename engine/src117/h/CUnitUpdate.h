// CUnitUpdate.h -- the free callees of the src117/lang/CUnitUpdate.cpp family, the
// per-frame order dispatch: RunOrders walks a unit's active order and calls one
// CUnit::Dispatch* handler per order id. The handlers are declared in CUnit.h with the
// rest of the class; these are the callees with no natural home in a class header.
#pragma once

#include "platform.h"   // ours: BYTE/WORD + <stddef.h> for offsetof

class CUnit;

// oracle 0x43A880 (90 B). A plain __fastcall taking the unit: the body reads its first
// argument out of ECX (`mov edi, ecx` before any stack read), and the ledger gives it no
// CUnit:: prefix. int, not void: both exits materialise `xor eax, eax`, and the callback's
// EAX is tested at its only registered call site, sub_442B60's `call ebx / test eax, eax`.
int apply_disruption_web_status(CUnit *pUnit);

// oracle 0x4B0BD0 (11 B), whole body: `mov dword ptr [0x6B6900], 0h / ret`.
// Nullary void.
void ai_reset_interceptor_call();

// oracle 0x49C0F0 (70 B). Returns a predicate: one exit `xor eax, eax`, the
// caller CUnit::EntityUpdate does `call / test eax, eax / jz`.
BOOL sub_49C0F0();

// oracle 0x4B4A10 (33 B). Not nullary: EntityUpdate calls it with ECX = the dead unit
// (`mov ecx, esi`), and although this body ignores ECX, its zero exit tail-jmps sub_4B47F0
// forwarding that ECX, which sub_4B47F0 reads as the unit. The parameter is there to
// express that passthrough; it does not change these bytes.
BOOL sub_4B4A10(CUnit *pUnit);
