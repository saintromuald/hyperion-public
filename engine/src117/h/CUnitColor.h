// CUnitColor.h -- the player-colour lookups src117/lang/CUnitColor.cpp defines.
//
// It exists so src117/gen/hook_table.cpp has a declaration for functions src117 already
// defines, without the generated TU having to include a reference-tree header, which
// pulled GDS/types.h behind it. It includes its own platform scaffold.
#pragma once

#include "platform.h"   // DWORD

// oracle 0x41D540 (6 B), whole body: mov eax, dword ptr [5D6E38h] / ret.
//
// No arguments: nothing is pushed and no register is read at the call site, and the body
// is a bare ret. Returns a DWORD, and the load width proves it -- mov eax, dword ptr, not
// movzx eax, byte ptr; the value is a colour index that fits in a byte, and
// rle_draw_playerside narrows to AL at its own call site.
//
// Defined in src117/lang/CUnitColor.cpp, and the TU placement is part of the contract: its
// caller rle_draw_playerside (0x40E950) is in CImage.cpp and 1.17 is WPO-off, so the oracle
// has to emit a real call. While this definition sat in CImage.cpp instead, /Ob2 spliced the
// six-byte body in and both playerside bodies came out 59 B against the oracle's 60. If one
// regresses to 59 B again, the TU placement is what broke.
DWORD get_active_unit_color();

// The rest of CUnitColor.cpp's free functions. A hooked free function has to be declared
// in a header and not only in the .cpp that defines it, or gen refuses to mark it.

// 0x41D4F0 (71 B). ECX read one byte wide; resolves a player's colour override.
BYTE __fastcall get_unit_color_override(BYTE bPlayer);

// 0x41D550 (10 B). Reads gbSelCircleColor_g @0x52904A at the player index.
BYTE __fastcall get_selection_circle_color(BYTE bPlayer);

// 0x41D560 (15 B). Player slot to colour index, through gdwColorSlotMap_g
// @0x5264FC, which is DWORD[8] -- the subscript is `[esi*4+5264FCh]`.
int __fastcall get_slot_mapping(int nSlot);

// 0x41D5D0 (186 B). ECX is the map's slot bytes and EDX their count.
void __fastcall permute_colors_by_map(void *pData, int nSize);
