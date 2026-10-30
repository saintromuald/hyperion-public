// CUnitHarvest.h -- the free functions of src117/lang/CUnitHarvest.cpp, the harvest block
// (oracle 0x4237A0 to 0x4247B0), and the callees in other TUs its bodies reach. All but one
// of the TU's bodies are CUnit methods, declared in src117/h/CUnit.h with the rest of the
// class. CUnit is not named for the one free function, which takes a unit type and not a
// unit, so this header does not forward-declare it.
#pragma once

#include "platform.h"   // ours: BOOL/BYTE/WORD/DWORD

// oracle 0x424420 (49 B). Spelled exactly as src117/lang/CUnitOrder.cpp already declares
// it, parameter name included. The type is a WORD: at 0x4297F0 `mov ecx, edi` moves a
// dword-read of a WORD slot with no widening, and an int parameter would have forced
// `movzx ecx, di`. This body never reads ECX above 16 bits either, comparing `cmp cx, ax`
// against 0xB0, 0xB1, 0xB2 and 0xBC with the type as the first operand, which is what a
// source-order `wUnitType == CONST` emits.
BOOL __fastcall IsTypeNeutralResource(WORD wUnitType);

// the callees in other TUs that this TU's bodies reach

// oracle 0x4461F0 (136 B), oracle TU gamesnd.cpp, unported. Two register arguments and one
// pushed (`ret 4h`), and void: no path defines EAX and the caller never reads it. The
// player is an int, not a BYTE -- CUnit::GetHarvest's call site widens it with
// `movzx ecx, byte ptr [esi+4Ch]` and the callee compares EDX as a dword -- and the sound
// id is a WORD, read with `movzx esi, dx`, which changes the 16-bit add its callers emit.
// Declared here, not in src117/h/gamesnd.h, to keep this TU's include closure at four.
void __fastcall gamesnd_show_player_error(int nPlayer, WORD wSoundId, int nStringId);

// 0x424590, 444 B, `ret 8h`: the worker in ECX, the refinery in EDX, then the
// two out-parameters. Its one caller tests the full EAX.
class CUnit;
#include "pos.h"  // Hyperion: mpoint_t
BOOL __fastcall sub_424590(CUnit *pUnit, CUnit *pGas, mpoint_t *pOut,
                           BYTE *pDirection);
