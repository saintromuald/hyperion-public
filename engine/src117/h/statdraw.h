// statdraw.h -- the interface of src117/lang/statdraw.cpp: the stat panel's
// draw and query layer. statdata.cpp owns the panel's control plumbing (find a
// node by index, show or hide a run of siblings, set a progress bar); this TU
// decides what goes in those nodes for the selected unit, and whether anything
// changed since the last frame.
//
// Include after <stddef.h> and "dialog.h" (ctrl_t).
#pragma once

#include "dialog.h"
#include "typelimits.h"  // weapon_id_t

// A forward declaration is enough: statdraw_query_needs_compbar never
// dereferences its CUnit *. `class`, as CUnit.h defines it; `struct` would be a
// C4099 in every TU that includes both.
class CUnit;

// The query layer: has anything the panel shows changed? statdraw_query_unit is
// the template: six bodies open with its four tests (saved hit points, saved
// shields, ownership, saved energy), and three more (archon, egg, carrier) are
// guards that fall through into it. Each returns an int its caller tests.
int __fastcall statdraw_query_archon();
int __fastcall statdraw_query_bldg();
int __fastcall statdraw_query_carrier();
int __fastcall statdraw_query_depot();
int __fastcall statdraw_query_egg();
int __fastcall statdraw_query_multi();
// statdraw_query_needs_compbar takes one register argument and ignores it,
// reloading 0x6AB4C4 itself. Its caller statdraw_save_transport loads ECX from
// the same global before the call and again after it, so the argument is real.
int __fastcall statdraw_query_needs_compbar(CUnit *pUnit);
int __fastcall statdraw_query_overlord();
int __fastcall statdraw_query_powerup();
int __fastcall statdraw_query_special();
int __fastcall statdraw_query_transport();
int __fastcall statdraw_query_unit();

// The save layer: a snapshot of what was drawn, the query layer's last state.
void __fastcall statdraw_save_stats();
void __fastcall statdraw_save_transport();
void __fastcall statdraw_save_unit();

// The draw layer. These bodies set EDX and never ECX before `call
// ctrl_find_ndx`, so they pass their own incoming ECX through: each takes the
// panel dialog node as its first parameter. They return void; ten of them end
// in a tail jump to ctrl_set_update.
void __fastcall statdraw_draw_carrier(ctrl_t *pDlg);
void __fastcall statdraw_draw_none(ctrl_t *pDlg);
void __fastcall statdraw_draw_overlord(ctrl_t *pDlg);
void __fastcall statdraw_draw_powerup(ctrl_t *pDlg);
void __fastcall statdraw_draw_transport(ctrl_t *pDlg);
void __fastcall statdraw_draw_under_construction(ctrl_t *pDlg);
void __fastcall statdraw_draw_unit(ctrl_t *pDlg);
void __fastcall statdraw_draw_unit_name(ctrl_t *pDlg);

// Both second parameters are unsigned 16-bit: the body zero-extends the value
// to index sgpszUnitStats, and statdraw_draw_stats walks the counter with an
// unsigned compare. A declaration with too few parameters would be an overload,
// not an error, so these two spellings matter.
void __fastcall statdraw_draw_shields(ctrl_t *pDlg, WORD nIndex);

// The third parameter is a stack byte: `ret 4h`, read one byte wide.
void __fastcall statdraw_draw_weapon(ctrl_t *pDlg, WORD nIndex, weapon_id_t bWeapon);

// The rest of the draw layer; most take only the panel node in ECX.
void __fastcall statdraw_draw_bldg(ctrl_t *pDlg);
void __fastcall statdraw_draw_depot(ctrl_t *pDlg);
void __fastcall statdraw_draw_hp(ctrl_t *pDlg);
void __fastcall statdraw_draw_kills(ctrl_t *pDlg);
void __fastcall statdraw_draw_properties(ctrl_t *pDlg);
void __fastcall statdraw_draw_stats(ctrl_t *pDlg);
void __fastcall statdraw_draw_transport_slots(ctrl_t *pDlg);
void __fastcall statdraw_draw_multi(ctrl_t *pDlg);
void __fastcall statdraw_draw_nuke(ctrl_t *pDlg, WORD nIndex);
void __fastcall statdraw_draw_archon(ctrl_t *pDlg);
void __fastcall statdraw_draw_addon(ctrl_t *pDlg);
void __fastcall statdraw_draw_egg(ctrl_t *pDlg);
void __fastcall statdraw_draw_mana(ctrl_t *pDlg);
void __fastcall statdraw_draw_silo(ctrl_t *pDlg);
void __fastcall statdraw_draw_researching_tech(ctrl_t *pDlg);
void __fastcall statdraw_draw_upgrade(ctrl_t *pDlg);
void __fastcall statdraw_draw_resources(ctrl_t *pDlg);
void __fastcall statdraw_draw_armor(ctrl_t *pDlg, WORD nIndex);
void __fastcall statdraw_draw_interceptors(ctrl_t *pDlg, WORD nIndex);
int __fastcall statdraw_draw_landmines(ctrl_t *pDlg, WORD nIndex);
void __fastcall statdraw_draw_training_unit(ctrl_t *pDlg);
void __fastcall statdraw_replay_progress(ctrl_t *pDlg);

// statdraw_draw_stat -- `ret 4h`: the third parameter is a stack dword, stored
// straight into ctrl_t::pData (+0x14), so it is a void *.
void __fastcall statdraw_draw_stat(ctrl_t *pDlg, short nIndex, void *pText);
