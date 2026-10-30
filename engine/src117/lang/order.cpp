// order.cpp -- the command-button handlers: what a command-card button does when
// the player clicks it, and the net command each one sends.
//
// No handler has a direct caller. Each is the ActionFn field of a 20-byte command
// button record in .data, so all 52 share one signature, `(WORD, BYTE)`, and most
// ignore one half of it; src117/h/order.h reads that signature off the data.
//
// Bodies are in ascending oracle address, which here is alphabetical by name for
// 49 of the 52; the three that are not say that Blizzard's names for them are not
// the ones we carry. Nothing in this file includes a reference-tree header, and an
// include that reaches cpp-1.17 from here is a regression.
#include <string.h>            // memset -- the oracle emits a real __cdecl call
#include <stddef.h>            // offsetof -- the headers' static_asserts need it

// The platform types, declared here rather than taken from <windows.h> so this
// TU's include set stays what it needs, and so CUnit.h sees BOOL, BYTE, WORD and
// DWORD before it is included.
typedef unsigned char  BYTE;
typedef unsigned short WORD;
#include "platform.h"  // DWORD
typedef int            BOOL;

#include "CSprite.h"           // ours: wPosX @+0x14, wPosY @+0x16
#include "CUnit.h"             // ours: pSprite @+0x0C, bPlayer @+0x4C, dwFlags @+0xDC
#include "target.h"            // ours: target_on
#include "placebox.h"          // ours: placebox_on
#include "rect16.h"            // ours: left/top/right/bottom, 16-bit signed
#include "CUnitHarvest.h"      // ours: gamesnd_show_player_error
#include "selectlimits.h"       // ours: SELECTION_MAX

// The game globals this TU reads: gpStatUnit, the status-pane unit, gpStatUnits,
// its twelve-unit selection, and g_LocalNationID. They come from the generated
// header, which has to sit after the game headers: it declares each global
// eagerly, so a type one of them names must already be complete.
#include "../gen/globals117.h"

// The status-pane selection holds 12 units: order_stim walks gpStatUnits from
// 0x6AB4C8 up to 0x6AB4F8, which is 0x30 bytes of pointers, and the unported
// order_filter_larva_units caps its own output at 12. The 0x6AB4F8 is not a
// global of its own, it is &gpStatUnits[12].
enum { MAX_SELECTION = SELECTION_MAX };

// The unported callees, each signature read off the callee's own body. All are
// __fastcall, and the keyword is spelled out although /Gr makes it the default:
// these state an ABI contract with code we have not authored, which should not
// depend on knowing the compiler flags.

// -- net command emitters ---------------------------------------------------
// Each ends in netmgr_queue_cmd(&cmd, len), and the `mov edx, <len>` in the callee
// is that packet's length, which gives the payload width: edx=1 is the opcode
// alone and no parameter, edx=2 the opcode and one BYTE, edx=3 the opcode and one
// WORD. All end in a plain `ret`.

// A trailing `opcode` comment is the immediate that callee stores as the first
// payload byte. Cloak, Return, UnloadAll, Morph, UpgradeBldg and Stim have none
// because nobody has read one off the oracle; it is left blank rather than
// guessed from the protocol tables.

// edx=1: no parameter.
void __fastcall send_net_cmd_BuildFighter();     // opcode 0x27
void __fastcall send_net_cmd_CancelNuke();       // opcode 0x2E
void __fastcall send_net_cmd_Kill();             // opcode 0x18
void __fastcall send_net_cmd_KillEgg();          // opcode 0x19
void __fastcall send_net_cmd_Stim();
void __fastcall send_net_cmd_StopAddon();        // opcode 0x34
void __fastcall send_net_cmd_StopResearch();     // opcode 0x31
void __fastcall send_net_cmd_StopUpgrade();      // opcode 0x33
void __fastcall send_net_cmd_Stop_Carrier();     // opcode 0x1B
void __fastcall send_net_cmd_Stop_Reaver();      // opcode 0x1C
void __fastcall send_net_cmd_SummonArchon();     // opcode 0x2A
void __fastcall send_net_cmd_SummonDArchon();    // opcode 0x5A
// No parameter either: opcode 0x2F plus the active unit's position, which this
// one reads from globals rather than taking as an argument.
void __fastcall send_net_cmd_Liftoff();

// edx=2: one BYTE parameter.
void __fastcall send_net_cmd_Burrow(BYTE bQueued);          // opcode 0x2C
void __fastcall send_net_cmd_Cloak(BYTE bQueued);
void __fastcall send_net_cmd_Deburrow(BYTE bQueued);        // opcode 0x2D
void __fastcall send_net_cmd_Decloak(BYTE bQueued);         // opcode 0x22
void __fastcall send_net_cmd_HoldPosition(BYTE bQueued);    // opcode 0x2B
void __fastcall send_net_cmd_Return(BYTE bQueued);
void __fastcall send_net_cmd_SiegeMode(BYTE bQueued);       // opcode 0x26
void __fastcall send_net_cmd_Stop(BYTE bQueued);            // opcode 0x1A
void __fastcall send_net_cmd_SwitchTankMode(BYTE bQueued);  // opcode 0x25
void __fastcall send_net_cmd_UnloadAll(BYTE bQueued);
void __fastcall send_net_cmd_Research(tech_id_t bTechId);        // opcode 0x30
void __fastcall send_net_cmd_Upgrade(upgrade_id_t bUpgradeId);      // opcode 0x32

// edx=3: one WORD parameter.
void __fastcall send_net_cmd_Build(WORD wUnitType);         // opcode 0x1F
void __fastcall send_net_cmd_Morph(WORD wUnitType);
void __fastcall send_net_cmd_StopBuild(WORD wSlot);         // opcode 0x20
void __fastcall send_net_cmd_UpgradeBldg(WORD wUnitType);

// -- order.cpp sibling ------------------------------------------------------
BOOL __fastcall order_can_cloak();
BOOL __fastcall order_can_cast_spell(tech_id_t bTech);

// -- targeting / placement UI ------------------------------------------------
// target_on (oracle 0x4B28B0), and the reading of its three BYTE arguments, are
// in target.h, included above.

// placebox_on (oracle 0x47D3A0), declared in src117/h/placebox.h, puts the cursor
// into building-placement mode. `mov bl, cl` makes its first argument a BYTE;
// `mov esi, edx` then `movzx eax, si` make the second 16-bit, and it indexes the
// units.dat flag table with it, so it is a unit type id. A plain `ret`, so there
// are no stack arguments.

// -- cost / affordability ----------------------------------------------------
// A BYTE player in CL, a 16-bit unit type in DX and `ret 8h` for two stack
// arguments; bldg_check_food and bldg_check_resources' verdict comes back in EAX.
// Hyperion: the unit type is an int, as banker.cpp's definition reads it,
// which the x64 build needs; a unit type is the same widened.
extern "C" BOOL __fastcall bldg_get_unit_cost(BYTE bPlayer, int nUnitType,
                                              BOOL bCheckFood, BOOL bReport);

// -- sound / message feedback ------------------------------------------------
// `mov esi, ecx` then `[esi+4Ch]` / `[esi+0DCh]` => a CUnit* in ECX.
void __fastcall gamesnd_ack(CUnit *pUnit);
// `mov esi, ecx` (CUnit*), `mov eax, edx` (a string, NULL-tested then handed to
// map_msg_target_self), `mov edx, dword ptr [ebp+8h]` compared against
// g_LocalNationID; `ret 4h` => exactly one stack argument.
void __fastcall gamesnd_error(CUnit *pUnit, char *pszMsg, DWORD dwNation);
// `mov edx, ecx` / `mov ecx, [sgpStatStrs]` / `jmp str_get_string` -- one
// register argument, NO stack argument (str_get_string is a plain `ret`).
// Returns the .tbl string pointer str_get_string computes. The id is a WORD;
// src117/h/order.h has the reading.
char *__fastcall status_get_str(WORD wStringId);

// -- unit iteration ----------------------------------------------------------
// `mov dword ptr [ebp-8h], ecx` then `movsx ... word ptr [ecx+2h]` => the rect
// arrives in ECX. The result is walked as a NULL-terminated CUnit* array by
// order_filter_larva_units, hence CUnit**.
CUnit **__fastcall get_units_in_rect(mrect_t *pRect);  // Hyperion: a box of map positions
// `mov edi, ecx` (the iterator), `mov dword ptr [edx+esi*4], eax` (the output
// array); returns the kept count in EAX.
int __fastcall order_filter_larva_units(CUnit **ppIter, CUnit **ppOut);
// Pops the unit-finder stack. Its body ignores ECX, but the call site loads it,
// so the declaration carries the argument.
void __fastcall finish_unit_iteration(CUnit **ppIter);
// `cmp esi, 1h` (ECX = count), `mov ecx, dword ptr [edi]` (EDX = the array),
// `push [ebp+0Ch]` / `push [ebp+8h]` into select_units; `ret 8h`.
void __fastcall sub_4BCE70(int nCount, CUnit **ppUnits, int a3, int a4);

// oracle 0x47C1B0. Copy the larvae of the status-pane hatchery out of a unit
// finder result, at most MAX_SELECTION of them.
int __fastcall order_filter_larva_units(CUnit **ppIter, CUnit **ppOut) {
  int nCount = 0;
  for (CUnit *pUnit = *ppIter; pUnit; pUnit = *++ppIter) {
    if (pUnit->wType == 0x23 && pUnit->pUnion80 == gpStatUnit &&
        !(pUnit->pSprite->flags.b & 0x20)) {
      ppOut[nCount++] = pUnit;
      if (nCount >= SelectionMax())  // Hyperion: 12, or 255 in Hyperion's selection (stage S)
        break;
    }
  }
  return nCount;
}

// oracle 0x47C200. TRUE when some selected unit has the energy for its cloak;
// otherwise report the race's not-enough-energy error. The shared result and
// the `break` put the 0x9C temp in ECX; a `return TRUE` in the loop gives EAX.
BOOL __fastcall order_can_cloak() {
  BOOL bReady = FALSE;
  for (int i = 0; i < SelectionMax(); i++) {  // Hyperion: selectlimits.h's (stage S)
    CUnit *pUnit = StatUnits()[i];
    if (pUnit) {
      int nCost = techdata_energy_g[pUnit->GetCloakingTech()] << 8;
      if (!pUnit->NeedsMoreEnergy(nCost)) {
        bReady = TRUE;
        break;
      }
    }
  }
  if (!bReady) {
    BYTE bPlayer = gpStatUnit->bPlayer;
    gamesnd_show_player_error(bPlayer, (WORD)(gpStatUnit->GetRace() + 0x9C),
                              (WORD)(gpStatUnit->GetRace() + 0x360));
  }
  return bReady;
}

// oracle 0x47C290. The same test for a spell whose tech is known up front.
BOOL __fastcall order_can_cast_spell(tech_id_t bTech) {
  int nCost = techdata_energy_g[bTech] << 8;
  BOOL bReady = FALSE;
  for (int i = 0; i < SelectionMax(); i++) {  // Hyperion: selectlimits.h's (stage S)
    CUnit *pUnit = StatUnits()[i];
    if (pUnit && !pUnit->NeedsMoreEnergy(nCost)) {
      bReady = TRUE;
      break;
    }
  }
  if (!bReady) {
    BYTE bPlayer = gpStatUnit->bPlayer;
    gamesnd_show_player_error(bPlayer, (WORD)(gpStatUnit->GetRace() + 0x9C),
                              (WORD)(gpStatUnit->GetRace() + 0x360));
  }
  return bReady;
}

// Attack: use the "attack unit" order when
//            anything in the selection can move, the static-defence order when
//            nothing can. The loop over the 12-entry selection is inlined.
void __fastcall order_attack(WORD, BYTE) {
  BYTE bOrder = 0x02;
  for (int i = 0; i < SelectionMax(); i++) {  // Hyperion: selectlimits.h's (stage S)
    CUnit *pUnit = StatUnits()[i];
    if (pUnit && pUnit->CanMove()) {
      bOrder = 0x0E;
      break;
    }
  }
  target_on(0x08, bOrder, 0x09);
}

// The same literal is passed twice; MSVC
//            materialises it once and copies it.
void __fastcall order_attack_unit(WORD, BYTE) {
  target_on(0x08, 0x08, 0x09);
}

void __fastcall order_bldg_liftoff(WORD, BYTE) {
  send_net_cmd_Liftoff();
}

// Forwards p1 -- it arrives in ECX and leaves
//            in ECX, so the bare `jmp` needs no register move.
void __fastcall order_build(WORD wUnitType, BYTE) {
  send_net_cmd_Build(wUnitType);
}

void __fastcall order_build_fighter(WORD, BYTE) {
  send_net_cmd_BuildFighter();
}

// Forwards p2, which arrives in DL, to the
//            callee in CL.
void __fastcall order_burrow(WORD, BYTE bQueued) {
  send_net_cmd_Burrow(bQueued);
}

void __fastcall order_cancel_addon(WORD, BYTE) {
  send_net_cmd_StopAddon();
}

void __fastcall order_cancel_research(WORD, BYTE) {
  send_net_cmd_StopResearch();
}

void __fastcall order_cancel_upgrade(WORD, BYTE) {
  send_net_cmd_StopUpgrade();
}

// Out of alphabetical order (see the file
//            header).
void __fastcall order_cancel_nuke(WORD, BYTE) {
  send_net_cmd_CancelNuke();
}

// Hyperion: the shared commands the carrier's and reaver's own buttons give among other
// kinds; defined below, in 1.17's order.
void __fastcall order_move(WORD, BYTE);
void __fastcall order_stop(WORD, BYTE bQueued);

void __fastcall order_carrier_attack(WORD w, BYTE b) {
  if (SelectionMixed()) {  // Hyperion: the shared Attack among other kinds (selectlimits.h)
    order_attack(w, b);
    return;
  }
  target_on(0x35, 0x0E, 0x36);
}

void __fastcall order_carrier_move(WORD w, BYTE b) {
  if (SelectionMixed()) {  // Hyperion: the shared Move among other kinds
    order_move(w, b);
    return;
  }
  target_on(0x31, 0x33, 0x33);
}

void __fastcall order_carrier_stop(WORD w, BYTE b) {
  if (SelectionMixed()) {  // Hyperion: the shared Stop among other kinds
    order_stop(w, b);
    return;
  }
  send_net_cmd_Stop_Carrier();
}

// The queued byte, p2, is kept in BL across
//            the predicate call.
void __fastcall order_cloak(WORD, BYTE bQueued) {
  if (order_can_cloak())
    send_net_cmd_Cloak(bQueued);
}

void __fastcall order_deburrow(WORD, BYTE bQueued) {
  send_net_cmd_Deburrow(bQueued);
}

void __fastcall order_decloak(WORD, BYTE bQueued) {
  send_net_cmd_Decloak(bQueued);
}

// Out of alphabetical order (see the file
//            header). Buildings get the other cursor pair; the sense of the test is
//            inverted relative to order_unload.
void __fastcall order_pickup(WORD, BYTE) {
  if (!(gpStatUnit->dwFlags & CUnit::UF_Building))
    target_on(0x5E, 0x06, 0x06);
  else
    target_on(0x5F, 0x17, 0x17);
}

void __fastcall order_harvest(WORD, BYTE) {
  target_on(0x4F, 0x06, 0x50);
}

// The same literal twice, as in order_attack_unit.
void __fastcall order_heal(WORD w, BYTE b) {
  if (SelectionMixed()) {  // Hyperion: the shared Attack among other kinds; a medic heal-moves
    order_attack(w, b);
    return;
  }
  target_on(0xB1, 0xB1, 0xB1);
}

void __fastcall order_hold_pos(WORD, BYTE bQueued) {
  send_net_cmd_HoldPosition(bQueued);
}

void __fastcall order_kill(WORD, BYTE) {
  send_net_cmd_Kill();
}

void __fastcall order_kill_egg(WORD, BYTE) {
  send_net_cmd_KillEgg();
}

// A tail call into placebox_on, whose `ret`
//            retires both: neither has stack arguments. The callee's second
//            argument is a WORD, so the unit type is copied in a 32-bit register.
void __fastcall order_land(WORD wUnitType, BYTE) {
  placebox_on(0x47, wUnitType);
}

void __fastcall order_morph(WORD wUnitType, BYTE) {
  if (bldg_get_unit_cost(gpStatUnit->bPlayer, wUnitType, 1, 1))
    send_net_cmd_Morph(wUnitType);
}

void __fastcall order_move(WORD, BYTE) {
  target_on(0x31, 0x06, 0x06);
}

// The same literal twice.
void __fastcall order_nuke_paint(WORD, BYTE) {
  target_on(0x7E, 0x7E, 0x7E);
}

// Tail call, as order_land.
void __fastcall order_nydus_exit(WORD wUnitType, BYTE) {
  placebox_on(0x2E, wUnitType);
}

// The same literal twice.
void __fastcall order_patrol(WORD, BYTE) {
  target_on(0x98, 0x98, 0x98);
}

void __fastcall order_place(WORD wUnitType, BYTE) {
  if (bldg_get_unit_cost(gpStatUnit->bPlayer, wUnitType, 1, 1))
    placebox_on(0x1E, wUnitType);
}

void __fastcall order_place_addon(WORD wUnitType, BYTE) {
  if (bldg_get_unit_cost(gpStatUnit->bPlayer, wUnitType, 1, 1))
    placebox_on(0x24, wUnitType);
}

// Takes the unit type off the status-pane
//            unit and ignores its p1 slot, unlike order_land / order_nydus_exit.
void __fastcall order_place_cop(WORD, BYTE) {
  placebox_on(0x9B, gpStatUnit->wType);
}

void __fastcall order_place_protoss(WORD wUnitType, BYTE) {
  if (bldg_get_unit_cost(gpStatUnit->bPlayer, wUnitType, 1, 1))
    placebox_on(0x1F, wUnitType);
}

void __fastcall order_rally(WORD, BYTE) {
  target_on(0x27, 0x28, 0x28);
}

void __fastcall order_reaver_attack(WORD w, BYTE b) {
  if (SelectionMixed()) {  // Hyperion: the shared Attack among other kinds
    order_attack(w, b);
    return;
  }
  target_on(0x3B, 0x0E, 0x3C);
}

// The same literal twice.
void __fastcall order_recharge(WORD, BYTE) {
  target_on(0x43, 0x43, 0x43);
}

void __fastcall order_repair(WORD, BYTE) {
  target_on(0x22, 0x06, 0x23);
}

void __fastcall order_return(WORD, BYTE bQueued) {
  gamesnd_ack(gpStatUnit);
  send_net_cmd_Return(bQueued);
}

// Out of alphabetical order (see the file
//            header). Forwards p1 narrowed to the callee's BYTE, which costs no
//            instruction. Hyperion: the whole WORD, a mod's tech past 255 too.
void __fastcall order_research(WORD wTechId, BYTE) {
  send_net_cmd_Research(wTechId);
}

// Select every larva near the status-pane
//            unit: build a 0x100-wide box around its sprite, ask the unit finder
//            for everything inside, keep the larvae, and select them.
void __fastcall order_select_larva(WORD, BYTE) {
  CUnit *ppUnits[SELECTION_WIDE];  // Hyperion: 12 in 1.17 (stage S)
  mrect_t rect;  // Hyperion: a box of map positions

  memset(ppUnits, 0, sizeof(ppUnits));
  CUnit *pUnit = gpStatUnit;
  rect.left   = pUnit->pSprite->wPosX - 0x80;
  rect.right  = pUnit->pSprite->wPosX + 0x80;
  rect.top    = pUnit->pSprite->wPosY - 0x80;
  rect.bottom = pUnit->pSprite->wPosY + 0x80;

  CUnit **ppIter = get_units_in_rect(&rect);
  int nCount = order_filter_larva_units(ppIter, ppUnits);
  finish_unit_iteration(ppIter);
  if (nCount)
    sub_4BCE70(nCount, ppUnits, 1, 1);
}

void __fastcall order_siege(WORD, BYTE bQueued) {
  send_net_cmd_SiegeMode(bQueued);
}

// Stim only if something in the selection can
//            take it; otherwise the UI error path, the one place that needs all
//            three globals.
void __fastcall order_stim(WORD, BYTE) {
  for (int i = 0; i < SelectionMax(); i++) {  // Hyperion: selectlimits.h's (stage S)
    CUnit *pUnit = StatUnits()[i];
    if (pUnit && pUnit->CanUseStim()) {
      send_net_cmd_Stim();
      return;
    }
  }
  gamesnd_error(gpStatUnit, status_get_str(0x36A), g_LocalNationID);
}

void __fastcall order_stop(WORD, BYTE bQueued) {
  send_net_cmd_Stop(bQueued);
}

void __fastcall order_stop_build(WORD wSlot, BYTE) {
  send_net_cmd_StopBuild(wSlot);
}

void __fastcall order_summon_archon(WORD, BYTE) {
  send_net_cmd_SummonArchon();
}

void __fastcall order_summon_darchon(WORD, BYTE) {
  send_net_cmd_SummonDArchon();
}

void __fastcall order_tank_mode(WORD, BYTE bQueued) {
  send_net_cmd_SwitchTankMode(bQueued);
}

// oracle 0x47C7B0. Enter targeting for a spell button, once the selection
// has the energy. Case order is the oracle's block order.
void __fastcall order_tech(WORD wTechId, BYTE) {
  if (!order_can_cast_spell((BYTE)wTechId))
    return;
  switch (wTechId) {
  case 0x04: target_on(0x8B, 0x8B, 0x8B); break;
  case 0x03: target_on(0x84, 0x84, 0x84); break;
  case 0x08: target_on(0x71, 0x71, 0x72); break;
  case 0x01: target_on(0x73, 0x73, 0x73); break;
  case 0x15: target_on(0x89, 0x89, 0x89); break;
  case 0x06: target_on(0x8D, 0x8D, 0x8D); break;
  case 0x0D: target_on(0x79, 0x79, 0x79); break;
  case 0x12: target_on(0x78, 0x78, 0x78); break;
  case 0x0C: target_on(0x1B, 0x06, 0x1C); break;
  case 0x02: target_on(0x7A, 0x7A, 0x7A); break;
  case 0x07: target_on(0x8F, 0x8F, 0x8F); break;
  case 0x11: target_on(0x92, 0x92, 0x92); break;
  case 0x16: target_on(0x93, 0x93, 0x93); break;
  case 0x0E: target_on(0x77, 0x77, 0x77); break;
  case 0x0F: target_on(0x90, 0x90, 0x90); break;
  case 0x10: target_on(0x91, 0x91, 0x91); break;
  case 0x13: target_on(0x8E, 0x8E, 0x8E); break;
  case 0x14: target_on(0x94, 0x94, 0x94); break;
  case 0x18: target_on(0xB4, 0xB4, 0xB4); break;
  case 0x19: target_on(0xB5, 0xB5, 0xB5); break;
  case 0x1B: target_on(0xB6, 0xB6, 0xB6); break;
  case 0x1D: target_on(0xB8, 0xB8, 0xB8); break;
  case 0x1E: target_on(0xB9, 0xB9, 0xB9); break;
  case 0x1F: target_on(0xBA, 0xBA, 0xBA); break;
  }
}

void __fastcall order_tower_attack(WORD, BYTE) {
  target_on(0x13, 0x12, 0x12);
}

void __fastcall order_trilobyte_stop(WORD w, BYTE b) {
  if (SelectionMixed()) {  // Hyperion: the shared Stop among other kinds
    order_stop(w, b);
    return;
  }
  send_net_cmd_Stop_Reaver();
}

// A building unloads everything; anything else
//            targets a drop point. dwFlags is a DWORD; MSVC narrows the `& 2` test.
void __fastcall order_unload(WORD, BYTE bQueued) {
  if (gpStatUnit->dwFlags & CUnit::UF_Building) {
    send_net_cmd_UnloadAll(bQueued);
    return;
  }
  target_on(0x70, 0x70, 0x70);
}

void __fastcall order_upgrade(WORD wUpgradeId, BYTE) {
  send_net_cmd_Upgrade(wUpgradeId);  // Hyperion: the whole WORD
}

void __fastcall order_upgrade_bldg(WORD wUnitType, BYTE) {
  if (bldg_get_unit_cost(gpStatUnit->bPlayer, wUnitType, 1, 1))
    send_net_cmd_UpgradeBldg(wUnitType);
}

void __fastcall order_zerg_place(WORD wUnitType, BYTE) {
  if (bldg_get_unit_cost(gpStatUnit->bPlayer, wUnitType, 1, 1))
    placebox_on(0x19, wUnitType);
}
