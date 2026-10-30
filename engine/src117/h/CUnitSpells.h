// CUnitSpells.h -- the free functions of src117/lang/CUnitSpells.cpp, the cloak,
// energy and status-effect core. The TU's CUnit members are declared in CUnit.h.
// The free functions here are __fastcall through /Gr and are declared without
// the keyword; a TU-local prototype in the .cpp spells it out.

#ifndef OC117_CUNITSPELLS_H
#define OC117_CUNITSPELLS_H

#include "platform.h"

class CUnit;

// 0x434300. Clears the cloak and burrow list head at 0x665D40 at startup.
void cunitspells_init();

// 0x434310. Walks the 0x665D40 list, reading pBurrowLinkNext before the call
// that may unlink the node, and calls status_set_update when anything changed.
void CloakUpdate();

// 0x434B20 / 0x434E30. The save codec pair for the list head; ECX is the file.
// Both round-trip the head through itself: they write a unit id into 0x665D40,
// read or write &sgpBurrowLinkHead_g, and convert back with CUnit::FromId.
BOOL LoadCloakList(void *pFile);
BOOL SaveCloakList(void *pFile);

// 0x435320 / 0x435380. get_spell_err_str takes the order id in ECX and the unit
// type in DX. get_target_spell_err_str takes the order id in ECX, the target in
// EDX and the acting player on the stack. sub_434130, the one caller, reads
// both results as 16 bits, so they return WORD.
WORD get_spell_err_str(int nOrderId, WORD wUnitType);

// The acting player as sub_434130 passes it: one byte stored over a dead slot
// and the whole dword pushed, which MSVC emits only for a one-byte aggregate.
struct spell_player_t {
  /*0x00*/ BYTE bPlayer;
};
static_assert(offsetof(spell_player_t, bPlayer) == 0x00, "spell_player_t.bPlayer");
static_assert(sizeof(spell_player_t) == 1, "spell_player_t is one byte");

WORD get_target_spell_err_str(int nOrderId, CUnit *pTarget, spell_player_t player);

#endif  // OC117_CUNITSPELLS_H
