#pragma once
// order.h -- declarations for src117/lang/order.cpp, the command-button order
// handlers at oracle 0x47C310 to 0x47CA80, with the few callees of theirs that
// no other header declares.
//
// Generated code names these: src117/gen/hook_table.cpp takes each handler's
// address, and oracle_thunks.cpp emits a jmp thunk for every callee that is not
// ported, so both need a declaration in a header. A prototype inside order.cpp
// is invisible to them, and a wrong signature mangles to a symbol that does not
// link.

#include "CUnit.h"
#include "rect16.h"

// 0x47C1B0, unported. Filters the selection down to the larvae and writes them
// out, returning how many. Both parameters are CUnit ** walked as
// pointer-to-pointer: the input walks gpStatUnits, the output is stored through.
// The names are ours, from what each one does.
int __fastcall order_filter_larva_units(CUnit **ppIter, CUnit **ppOut);

// 0x47C200, unported, called by order_cloak. It takes no argument: the body
// reads the selection array gpStatUnits itself, and neither ECX nor EDX is read
// before being written. The flag comes back in EAX as a 0 or 1.
BOOL __fastcall order_can_cloak();

// 0x47C290. order_can_cloak's loop with the tech passed in: `movzx eax, cl`
// indexes techdata_energy_g, so a BYTE in ECX. Called by order_tech.
BOOL __fastcall order_can_cast_spell(tech_id_t bTech);

// The next four are unported callees whose signatures come from the oracle. The
// reference tree declares them too, with other parameter types, and a parameter
// type is part of the mangled symbol, so these spellings are the ones the bodies
// here were compiled against and must not be reconciled toward another's without
// re-checking those bodies.
void  __fastcall gamesnd_error(CUnit *pUnit, char *pszMsg, DWORD dwNation);
// The string id is a WORD, not an int: str_get_string, which this and
// get_map_string tail-jump into, only ever touches it as DX, and an int makes
// MSVC zero-extend it in place at get_unit_name_from_type's tail jump, which the
// oracle does not do. The return stays `char *`, since order.cpp hands it
// straight to gamesnd_error(CUnit *, char *, DWORD).
char *__fastcall status_get_str(WORD wStringId);

// 0x48D690. Shows one of the targeting error messages for a player. Two register
// parameters and nothing pushed: ECX is the message, status_get_str's `char *`
// return, and EDX a player id, compared as a DWORD against the dword at
// 0x508D50. Nothing reads EAX. It is declared here, beside status_get_str,
// because a callee that generated code may thunk needs exactly one spelling in
// one header.
void __fastcall map_msg_target_self(char *pszMsg, DWORD dwNation);
CUnit **__fastcall get_units_in_rect(mrect_t *pRect);
#if defined(_WIN64) || defined(__LP64__)
// Hyperion: 1.17's box, for the callers that still take it.
inline CUnit **get_units_in_rect(rect16_t *pRect) {
  mrect_t rc = *pRect;
  return get_units_in_rect(&rc);
}
#endif
void  __fastcall sub_4BCE70(int nCount, CUnit **ppUnits, int a3, int a4);

// The order handlers themselves. Each is the ActionFn field of a 20-byte command
// button record, 53 of them across 533 slots in .data, and a struct field has one
// type, so the whole family shares the `(WORD, BYTE)` signature. The second
// parameter is a BYTE because order_cloak and order_return save it across a call
// in BL; the first is a units.dat id, and its width is a reading of what the ids
// are rather than of the code. A parameter a handler ignores is left unnamed.

void __fastcall order_attack(WORD, BYTE);
void __fastcall order_attack_unit(WORD, BYTE);
void __fastcall order_bldg_liftoff(WORD, BYTE);
void __fastcall order_build(WORD wUnitType, BYTE);
void __fastcall order_build_fighter(WORD, BYTE);
void __fastcall order_burrow(WORD, BYTE bQueued);
void __fastcall order_cancel_addon(WORD, BYTE);
void __fastcall order_cancel_research(WORD, BYTE);
void __fastcall order_cancel_upgrade(WORD, BYTE);
void __fastcall order_cancel_nuke(WORD, BYTE);
void __fastcall order_carrier_attack(WORD, BYTE);
void __fastcall order_carrier_move(WORD, BYTE);
void __fastcall order_carrier_stop(WORD, BYTE);
void __fastcall order_cloak(WORD, BYTE bQueued);
void __fastcall order_deburrow(WORD, BYTE bQueued);
void __fastcall order_decloak(WORD, BYTE bQueued);
void __fastcall order_pickup(WORD, BYTE);
void __fastcall order_harvest(WORD, BYTE);
void __fastcall order_heal(WORD, BYTE);
void __fastcall order_hold_pos(WORD, BYTE bQueued);
void __fastcall order_kill(WORD, BYTE);
void __fastcall order_kill_egg(WORD, BYTE);
void __fastcall order_land(WORD wUnitType, BYTE);
void __fastcall order_morph(WORD wUnitType, BYTE);
void __fastcall order_move(WORD, BYTE);
void __fastcall order_nuke_paint(WORD, BYTE);
void __fastcall order_nydus_exit(WORD wUnitType, BYTE);
void __fastcall order_patrol(WORD, BYTE);
void __fastcall order_place(WORD wUnitType, BYTE);
void __fastcall order_place_addon(WORD wUnitType, BYTE);
void __fastcall order_place_cop(WORD, BYTE);
void __fastcall order_place_protoss(WORD wUnitType, BYTE);
void __fastcall order_rally(WORD, BYTE);
void __fastcall order_reaver_attack(WORD, BYTE);
void __fastcall order_recharge(WORD, BYTE);
void __fastcall order_repair(WORD, BYTE);
void __fastcall order_return(WORD, BYTE bQueued);
void __fastcall order_research(WORD wTechId, BYTE);
void __fastcall order_select_larva(WORD, BYTE);
void __fastcall order_siege(WORD, BYTE bQueued);
void __fastcall order_stim(WORD, BYTE);
void __fastcall order_stop(WORD, BYTE bQueued);
void __fastcall order_stop_build(WORD wSlot, BYTE);
void __fastcall order_summon_archon(WORD, BYTE);
void __fastcall order_summon_darchon(WORD, BYTE);
void __fastcall order_tank_mode(WORD, BYTE bQueued);
void __fastcall order_tech(WORD wTechId, BYTE);
void __fastcall order_tower_attack(WORD, BYTE);
void __fastcall order_trilobyte_stop(WORD, BYTE);
void __fastcall order_unload(WORD, BYTE bQueued);
void __fastcall order_upgrade(WORD wUpgradeId, BYTE);
void __fastcall order_upgrade_bldg(WORD wUnitType, BYTE);
void __fastcall order_zerg_place(WORD wUnitType, BYTE);
