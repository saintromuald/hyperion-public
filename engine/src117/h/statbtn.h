// statbtn.h -- declarations for src117/lang/statbtn.cpp: the command-card
// button filters (0x49F410-0x4A0610), the predicates the button table consults
// to decide whether a slot is enabled, hidden or shown unavailable, and the
// in-progress bitset accessors that live with them.
#pragma once

#include "CUnit.h"

// Two ABIs. The filters take three parameters, two in ECX/EDX and one on the
// stack (`ret 4h`); most ignore ECX and EDX and leave them unnamed. The bitset
// accessors take two registers and a plain `ret`.

// A filter's tail jump shows its callee takes the same three parameters: MSVC
// turns a call into a jmp only when the stack cleanup matches. That is where
// the *Allowed predicates below get their third parameter.

// The filters return one of three states, not a BOOL: bf_can_make_archon
// returns 0, 1 or -1 from one body. The reading 1 enabled, 0 hidden, -1 shown
// but unavailable is inferred from behaviour: the filters are reached through
// the button table in .data, and no dispatch site in .text shows it.

// Callees in other TUs, declared as their call sites and bodies show.

// The *Allowed predicates take three parameters, the third on the stack. The
// first is int-width in the last three: the callers widen it with movzx or load
// a constant full width, and UpgradeAllowed reads all 32 bits. OrderAllowed's
// id is a WORD: its body passes it on to CheckAllowed unwidened, and
// placebox_validate keeps its BYTE order id in BL only when this call widens to
// 16 bits.
int __fastcall OrderAllowed(WORD nOrder, int nPlayer, CUnit *pUnit);       // 0x407BB0
int __fastcall ResearchAllowed(int nResearch, int nPlayer, CUnit *pUnit);  // 0x407DC0
int __fastcall TechUseAllowed(int nTech, int nPlayer, CUnit *pUnit);       // 0x407F90
int __fastcall UpgradeAllowed(int nUpgrade, int nPlayer, CUnit *pUnit);    // 0x408140

// TTAllowed (0x4B32A0) takes a WORD: it reads CX as a 16-bit unit type,
// compared against 228, the units.dat count, and bf_empty_silo tail-jumps to it
// without widening CL, where bf_research, bf_has_spell and bf_upgrade widen
// first. So the filters are not uniform in their first parameter: bf_research's
// is a BYTE, bf_empty_silo's a WORD, and one of them is a cast in Blizzard's
// button table.
int __fastcall TTAllowed(WORD wUnitType, int nPlayer, CUnit *pUnit);

// oracle 0x4B2840. Returns the byte at 0x6BE650 in AL; evt_target_off tests AL
// only. No parameters.
BYTE __fastcall target_is_on();

// oracle 0x47D070. Returns the dword at 0x6751F4 in EAX; order_cancel_land
// tests all 32 bits. No parameters.
int __fastcall placebox_is_on();

// oracle 0x47D290 (106 B). order_cancel_land tail-jumps to it after
// `xor dl, dl` and `mov ecx, 1h`. The second parameter is a BYTE (the 8-bit
// xor); the first is an int, as the call site's full-width load needs, though
// the body tests only CL.
void __fastcall sub_47D290(int nMode, BYTE bFlag);

// The TU's own functions, in 1.17's .text order; each body in the .cpp gives
// its address and size. bf_building_ok and bf_empty_silo take a WORD first
// parameter: both forward ECX to TTAllowed unwidened. Neither body reads it, so
// a BYTE here would change behaviour without a compile error.
int  __fastcall bf_always(BYTE, int, CUnit *);                       // 0x49F410
int  __fastcall bf_building_ok(WORD wUnitType, int nPlayer, CUnit *pUnit);  // 0x49F420
int  __fastcall bf_build_fighter_ok(int nUnitType, int nPlayer, CUnit *pUnit);  // 0x49F460
int  __fastcall bf_can_move_patrol(BYTE, int, CUnit *);              // 0x49F4C0
int  __fastcall bf_can_move_stop(BYTE, int, CUnit *);                // 0x49F500
// The noinline is repeated here because statbtn.cpp sees this declaration
// first: without it /Ob2 splices the body into bf_can_attack_unburrowed.
__declspec(noinline) int  __fastcall bf_can_attack(BYTE, int, CUnit *);                   // 0x49F530
int  __fastcall bf_tower(BYTE, int, CUnit *);                        // 0x49F570
int  __fastcall bf_stop_unit(BYTE, int, CUnit *pUnit);               // 0x49F5D0
int  __fastcall bf_stop_addon(BYTE, int, CUnit *pUnit);              // 0x49F5F0
int  __fastcall bf_larva_sel(BYTE, int, CUnit *pUnit);               // 0x49F600
int  __fastcall bf_rally(BYTE, int nPlayer, CUnit *pUnit);           // 0x49F640
int  __fastcall bf_hatch_rally(BYTE, int nPlayer, CUnit *pUnit);     // 0x49F650
int  __fastcall bf_not_comp(BYTE, int, CUnit *pUnit);                // 0x49F690
int  __fastcall bf_stop_upgrade(BYTE, int, CUnit *pUnit);            // 0x49F6B0
int  __fastcall bf_stop_research(BYTE, int, CUnit *pUnit);           // 0x49F6D0
int  __fastcall bf_no_load(BYTE, int, CUnit *);                      // 0x49F6F0
int  __fastcall bf_load(BYTE, int, CUnit *);                         // 0x49F720
int  __fastcall bf_trans(BYTE, int, CUnit *);                        // 0x49F750
int  __fastcall bf_trans_has_passenger(BYTE, int, CUnit *);          // 0x49F790
int  __fastcall bf_research(tech_id_t bResearch, int nPlayer, CUnit *pUnit);   // 0x49F7C0
int  __fastcall bf_has_spell(tech_id_t bTech, int nPlayer, CUnit *pUnit);      // 0x49F7D0
int  __fastcall bf_has_mines(BYTE bTech, int nPlayer, CUnit *);      // 0x49F7E0
int  __fastcall bf_upgrade(upgrade_id_t bUpgrade, int nPlayer, CUnit *pUnit);     // 0x49F840
int  __fastcall bf_can_cloak(BYTE bTech, int nPlayer, CUnit *);     // 0x49F850
int  __fastcall bf_is_cloaked(BYTE bTech, int nPlayer, CUnit *);    // 0x49F8C0
int  __fastcall bf_can_cloak_hero(BYTE, int nPlayer, CUnit *);      // 0x49F930
int  __fastcall bf_is_hero_cloaked(BYTE, int nPlayer, CUnit *);     // 0x49F9A0
int  __fastcall bf_can_siege(BYTE bTech, int nPlayer, CUnit *pUnit);      // 0x49FA10
int  __fastcall bf_is_sieged(BYTE bTech, int nPlayer, CUnit *pUnit);      // 0x49FA60
int  __fastcall bf_can_tank_move(BYTE, int, CUnit *);                // 0x49FAB0
int  __fastcall bf_has_nuke(BYTE, int, CUnit *pUnit);                // 0x49FAE0
int  __fastcall bf_battery(BYTE, int, CUnit *pUnit);                 // 0x49FB00
int  __fastcall bf_is_ship(BYTE, int, CUnit *pUnit);                 // 0x49FB40
int  __fastcall bf_lifted(BYTE, int, CUnit *pUnit);                  // 0x49FB60
int  __fastcall bf_liftoff_ok(BYTE, int, CUnit *pUnit);              // 0x49FB90
int  __fastcall bf_can_burrow(BYTE bTech, int nPlayer, CUnit *);     // 0x49FBF0
int  __fastcall bf_burrowed(BYTE bTech, int nPlayer, CUnit *);       // 0x49FC70
int  __fastcall bf_make_lurker(BYTE, int nPlayer, CUnit *pUnit);     // 0x49FCE0
int  __fastcall bf_can_move_patrol_unburrowed(BYTE, int, CUnit *);   // 0x49FCF0
int  __fastcall bf_can_attack_unburrowed(BYTE bId, int nPlayer, CUnit *pUnit);    // 0x49FD20
int  __fastcall bf_no_load_drone(BYTE, int, CUnit *);                // 0x49FD70
int  __fastcall bf_load_drone(BYTE, int, CUnit *);                   // 0x49FDC0
int  __fastcall bf_can_move_stop_unburrowed(BYTE, int, CUnit *);     // 0x49FE10
int  __fastcall bf_no_exit(BYTE, int, CUnit *pUnit);                 // 0x49FE60
int  __fastcall bf_zerg_build_basic(BYTE, int nPlayer, CUnit *pUnit);     // 0x49FE80
int  __fastcall bf_zerg_build_advanced(BYTE, int nPlayer, CUnit *pUnit);  // 0x49FF20
int  __fastcall bf_has_interceptor(BYTE, int, CUnit *);              // 0x49FFB0
int  __fastcall bf_has_scarab(BYTE, int, CUnit *);                   // 0x49FFF0
int  __fastcall bf_can_make_archon_need2(BYTE bTech, int nPlayer, CUnit *pUnit);  // 0x4A0030
int  __fastcall bf_can_make_archon(BYTE, int nPlayer, CUnit *pUnit);      // 0x4A0050
int  __fastcall bf_can_make_darchon(BYTE, int nPlayer, CUnit *pUnit);     // 0x4A0090
int  __fastcall bf_protoss_build_basic(BYTE, int nPlayer, CUnit *pUnit);     // 0x4A00D0
int  __fastcall bf_protoss_build_advanced(BYTE, int nPlayer, CUnit *pUnit);  // 0x4A0180
int  __fastcall bf_stop_build(BYTE, int, CUnit *);                   // 0x4A0230
int  __fastcall bf_scv_attack_orders(BYTE, int, CUnit *);            // 0x4A0260
int  __fastcall bf_no_load_scv(BYTE, int, CUnit *);                  // 0x4A0290
int  __fastcall bf_load_scv(BYTE, int, CUnit *);                     // 0x4A02F0
int  __fastcall bf_terran_build_basic(BYTE, int nPlayer, CUnit *pUnit);      // 0x4A0350
int  __fastcall bf_terran_build_advanced(BYTE, int nPlayer, CUnit *pUnit);   // 0x4A0410
int  __fastcall bf_empty_silo(WORD wUnitType, int nPlayer, CUnit *pUnit);         // 0x4A0490
// evt_target_off (0x4A04B0) sits here in the oracle's order; it is declared
// below.
void __fastcall order_cancel_land(WORD, BYTE);                       // 0x4A04C0
int  __fastcall unit_is_cloaked_by_arbiter(CUnit *pUnit);            // 0x4A04E0

// The six in-progress bitset accessors, in the oracle's .text order.
void __fastcall clear_tech_in_progress(tech_id_t bTech, int nPlayer);     // 0x4A0500
void __fastcall clear_upgrade_in_progress(upgrade_id_t bUpgrade, int nPlayer);  // 0x4A0530
int  __fastcall is_tech_in_progress(tech_id_t bTech, int nPlayer);        // 0x4A0560
int  __fastcall is_upgrade_in_progress(upgrade_id_t bUpgrade, int nPlayer);  // 0x4A0590
void __fastcall set_tech_in_progress(tech_id_t bTech, int nPlayer);       // 0x4A05C0
void __fastcall set_upgrade_in_progress(upgrade_id_t bUpgrade, int nPlayer); // 0x4A05F0

// 0x4A04B0 (14 B).
void __fastcall evt_target_off();

// statcmd_set_ctrl (0x4A11E0) is declared in statcmd.h, beside its body.

// 0x4B38C0 (77 B), in textbox.cpp, which leaves its declaration here. ECX
// only, a `const char *`: the callers pass error_get_str's result or NULL. It
// ends in a tail jump to ctrl_set_update.
void __fastcall targeting_msg(const char *pszMsg);        // 0x4B38C0  77 B
