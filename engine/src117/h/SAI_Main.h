// SAI_Main.h -- the SAI_Main.cpp entry points src117 code calls, and the callees
// that TU reaches which no other header of ours declares.
//
// Every one of these names is declared in a header, not only in the .cpp that
// defines it: a prototype in a .cpp is not a seat another TU can include, and
// the payload link is the only layer that sees the failure. Each is declared in
// exactly one header too -- two spellings at one include root make the callee
// ambiguous and emit no thunk.
#pragma once

#include "platform.h"

class CUnit;

// SAI_RemoveUnit -- 0x48DBC0, 43 B. __fastcall with the unit in ECX and the flag
// in EDX, nothing pushed; EAX is dead at the epilogue, so void. The flag is
// `int` because this body only forwards it to SAI_Banker_RemoveUnit and never
// reads a width of its own, and a register argument occupies the whole register
// at every width.
void __fastcall SAI_RemoveUnit(CUnit *pUnit, int nFlag);

// ai_mark_targets_stale -- 0x48DD40, 11 B. CUnit::DispatchDoorOpen tail-jumps
// into it with `mov ecx, esi` and nothing pushed, so it takes the unit in ECX.
void __fastcall ai_mark_targets_stale(CUnit *pUnit);

// sai_main_free -- 0x48D380, 20 B. Nullary; EAX at the ret is inherited and no
// caller reads it -> void.
void sai_main_free();

// sai_init -- 0x48DB70, 68 B. Nullary; EAX at the ret is sai_build_init's
// leftover and no caller reads it -> void.
void sai_init();

// read_save_ai_data -- 0x48DBF0, 87 B. Two arguments; every ret materialises an
// explicit 0/1 -> BOOL.
BOOL read_save_ai_data(intptr_t nArg0, int nArg1);

// aiscript_switch_rescue -- 0x48DC50, 148 B. The one register argument is a
// player index, not a pointer: ECX's only three uses are `lea eax, [ecx+ecx*8h]`,
// `lea eax, [ecx+ecx*2h]` and `lea esi, [ecx+534914h]`, the base of the byte
// alliance matrix cursor.cpp's is_enemy_unit indexes by player; the later
// `mov ecx, esi` reloads that address, not the argument. void.
void aiscript_switch_rescue(int nPlayer);

// aiscript_move_dt -- 0x48DCF0, 71 B. Three arguments; ECX is forwarded to a
// callee untouched, so this body shows no width for it -- `int` occupies the
// whole register at every width. void.
void aiscript_move_dt(int nArg0, int nArg1, int nArg2);

// ai_mark_targets_stale_on_death -- 0x48DD50, 31 B. One register argument in ECX,
// dereferenced; pointee unnamed by the bytes. void.
void ai_mark_targets_stale_on_death(void *pArg0);

// init_unit_threat_scores -- 0x48DD70, 5 B. Nullary, void.
void init_unit_threat_scores();

// sai_main_init -- 0x48DD80, 15 B. Nullary; EAX at the ret is sai_seed_town_plans's
// leftover and no caller reads it -> void.
void sai_main_init();

// sai_update -- 0x48DD90, 20 B. Nullary; EAX at the ret is an incidental
// 32-bit write -> void.
void sai_update();

// sai_write_save_data -- 0x48DDB0, 78 B. One argument; every ret materialises
// an explicit 0/1 -> BOOL.
BOOL sai_write_save_data(intptr_t nArg0);

// ai_set_alliance_in_area -- 0x48DE00, 108 B. Three arguments; EDX is
// dereferenced (pointee unnamed by the bytes). void.
void ai_set_alliance_in_area(int nArg0, void *pArg1, int nArg2);

// ai_try_burrow / ai_try_siege -- 0x48DE70 / 0x48DEE0, 108 B each. One 32-bit
// argument; void.
void ai_try_burrow(intptr_t nArg0);
void ai_try_siege(intptr_t nArg0);

// ai_periodic_timer_tick -- 0x48DF50, 153 B. Nullary; EAX at the ret is an incidental
// 32-bit write -> void.
void ai_periodic_timer_tick();

// ai_nearest_enemy_owner is not declared here: SAI_Banker.h already seats it as
// `int ai_nearest_enemy_owner(int nation, int nTarget)`, the same contract. The
// seat belongs in this header on ownership grounds and moving it is its own
// change.

// aiscript_ally / aiscript_enemy -- 0x48E160 / 0x48E170, 8 B each. Both forward
// both register arguments to ai_set_alliance_in_area untouched, so neither body
// shows a width for either -- `int` occupies the whole register at every width.
// void.
void aiscript_ally(int nArg0, intptr_t nArg1);
void aiscript_enemy(int nArg0, intptr_t nArg1);

// ai_update_unit_idle -- 0x48E180, 809 B. One argument; every ret materialises 0/1.
BOOL ai_update_unit_idle(CUnit *pUnit);

// ai_can_use_tech_order -- 0x48E580, 59 B. The unit, an order id and a tech id.
// The callee reads the order one byte wide, but ai_update_unit_spells loads it
// with `mov edx, imm32` at every site; a BYTE parameter emits `mov dl`.
int ai_can_use_tech_order(CUnit *pUnit, int nOrder, int nTech);

// ai_unit_is_pursuing_order -- 0x48E5C0, 63 B. The caster, the order, its
// target and the target's asked-for bit (arg3, read one byte wide). BOOL.
BOOL ai_unit_is_pursuing_order(CUnit *pUnit, int nOrder, CUnit *pTarget, BYTE bAsked);

// ai_update_unit_spells -- 0x48D3A0, 1883 B. Tries the unit type's spells in
// order and returns TRUE once one is ordered. nMode is forwarded to the target
// searches; zero also consults the banker (banker_ai_spells_allowed) first.
BOOL __fastcall ai_update_unit_spells(CUnit *pUnit, int nMode);

// Below: the callees in other oracle TUs that no header of ours declared.

// sai_scripts_free / sai_scripts_update -- oracle TU SAI_Scripts.cpp.
// Nullary, void. sai_scripts_init and the script save codec pair are seated
// in SAI_Scripts.h.
void sai_scripts_free();
void sai_scripts_update();

// tai_compute_unit_threat_scores -- oracle TU TAI_AreaStrength.cpp. Nullary,
// void.
void tai_compute_unit_threat_scores();

// worker_tick -- oracle 0x487580, 427 B, TU SAI_Build.cpp. One register
// argument; its one caller discards EAX.
void worker_tick(CUnit *pUnit);

