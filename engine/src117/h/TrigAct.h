// TrigAct.h -- the interface of src117/lang/TrigAct.cpp, the trigger action
// handlers (oracle 0x4B5620..0x4B8E70). A trigger's action list is an array of
// 32-byte records; trigger_execute (0x4BDF10) dispatches each one on its action
// byte to a handler taking `TrigAction *` in ECX.
//
// Two names this TU uses are declared elsewhere on purpose: TTAllowed in
// src117/h/statbtn.h, and SErrSetLastError, a Storm import, in the .cpp.
// get_map_string returns `const char *` and run_aiscript's third parameter is
// `const rect32_t *`, as src117/lang/CUnitMisc.cpp and
// src117/lang/gametype.cpp already declare them: C++ mangles both into the
// decorated name, so a second spelling is a second symbol.
#pragma once

#include "CUnit.h"
#include "CUnitInit.h"   // CreateUnitAt
#include "rect32.h"   // run_aiscript's third parameter, supplied here because
                      // src117/lang/gametype.cpp includes this header before
                      // rect32.h, and a header that names a type must supply it
#include "gds_mou_win.h"

// TrigAction -- one trigger action record, 32 B, 64 to a trigger; the array's
// own offset inside the trigger is 0x148. The fields the handlers read:
//   bRegion     a one-based map region ("location") index, 0 for none; every
//               use decrements it into gMapRegions' zero-based index.
//   dwStringId  a DWORD whose low word is a map-string id: act_next_mission
//               guards on the whole dword and narrows for get_map_string.
//   dwTime      a delay in milliseconds (show_portrait, SetDelayedAction).
//   dwGroup     a trigger group code, the space TrigAct_KillRemoveUnits
//               switches over at 0x0D..0x1A.
//   bCount      the count, or 0 for all, which each caller spells with its own
//               sentinel (0xFFFF in the sextet, 0xF423F in kill/remove).
// The unread bytes are held at true size so these keep their offsets.
struct TrigAction {
  /*0x00*/ BYTE  bRegion;
  /*0x01*/ BYTE  _unread_01[0x03];
  /*0x04*/ DWORD dwStringId;
  /*0x08*/ BYTE  _unread_08[0x04];
  /*0x0C*/ DWORD dwTime;
  /*0x10*/ DWORD dwGroup;
  /*0x14*/ DWORD dwValue;
  /*0x18*/ WORD  wUnitType;
  /*0x1A*/ BYTE  bAction;
  /*0x1B*/ BYTE  bCount;
  /*0x1C*/ BYTE  bFlags;
  /*0x1D*/ BYTE  _unread_1D[0x03];
};
static_assert(sizeof(TrigAction)              == 0x20, "TrigAction stride (trigger_execute `shl eax,5`)");
static_assert(offsetof(TrigAction, bRegion)   == 0x00, "TrigAction::bRegion   [ecx]");
static_assert(offsetof(TrigAction, dwStringId) == 0x04, "TrigAction::dwStringId [ecx+4h]");
static_assert(offsetof(TrigAction, dwTime)    == 0x0C, "TrigAction::dwTime    [esi+0Ch]");
static_assert(offsetof(TrigAction, dwGroup)   == 0x10, "TrigAction::dwGroup   [esi+10h]");
static_assert(offsetof(TrigAction, dwValue)   == 0x14, "TrigAction::dwValue   [esi+14h]");
static_assert(offsetof(TrigAction, wUnitType) == 0x18, "TrigAction::wUnitType [esi+18h]");
static_assert(offsetof(TrigAction, bAction)   == 0x1A, "TrigAction::bAction   [edx+162h]");
static_assert(offsetof(TrigAction, bCount)    == 0x1B, "TrigAction::bCount    [esi+1Bh]");
static_assert(offsetof(TrigAction, bFlags)    == 0x1C, "TrigAction::bFlags    [eax+esi+164h]");

// TrigRegionId -- TrigFindFirstUnitAt's third parameter, a one-byte aggregate
// rather than a scalar. Its callers store one byte into a 4-byte local and push
// the whole dword (`mov byte ptr [ebp-4h], al / push dword ptr [ebp-4h]`), so
// three of the four pushed bytes are stale stack; the callee narrows with
// `movzx eax, al`, which makes the garbage harmless but read -- a 1.17 quirk
// kept on purpose. A BYTE or char parameter emits `movzx eax, al / push eax`
// with no frame instead, so the shape needs the aggregate. The name is ours and
// describes only the value it carries: the zero-based region index, after the
// callers' `dec`.
struct TrigRegionId {
  /*0x00*/ BYTE bRegion;   // zero-based; every caller `dec`s the action's
                           // one-based TrigAction::bRegion into it
};
static_assert(sizeof(TrigRegionId)             == 0x01, "TrigRegionId is ONE byte (`mov byte ptr [ebp-4h], al` is its whole initialisation)");
static_assert(offsetof(TrigRegionId, bRegion)  == 0x00, "TrigRegionId::bRegion [ebp-4h]");

// The two callback ABIs of this TU, which are not the same ABI.

// The per-unit property setter -- `cb(pUnit, nValue)`, __stdcall with callee
// cleanup: the caller pushes the value then the unit and cleans nothing, and
// every body reads [ebp+8h] and [ebp+0Ch] and retires with `ret 8h`, so the
// unit is first and nothing rides in a register. The result is consumed
// (`test eax, eax`). The two argument-passing blocks below are seated here, not
// in the .cpp, because the generated thunks name them and see only headers.

// sub_4B7330's out-parameter: two ints, the centre or the found unit's sprite
// position. `sub esp, 8h` in act_move_loc, `mov dword ptr [ecx], eax` and
// `mov dword ptr [ecx+4h], eax` in the callee.
struct TrigPoint {
  /*0x00*/ int x;
  /*0x04*/ int y;
};
static_assert(sizeof(TrigPoint)        == 0x08, "TrigPoint is act_move_loc's `sub esp, 8h`");
static_assert(offsetof(TrigPoint, x)   == 0x00, "TrigPoint::x [ebp-8h]");
static_assert(offsetof(TrigPoint, y)   == 0x04, "TrigPoint::y [ebp-4h]");

// act_create_unit's two-dword block, passed to Trig_CreateUnitWithProperties as
// its one stack argument: `mov dword ptr [ebp-8h], ecx` takes the action's
// dwValue, `mov dword ptr [ebp-4h], ecx` the zero-based region.
struct TrigCreateProps {
  /*0x00*/ DWORD dwValue;
  /*0x04*/ DWORD dwRegion;
};
static_assert(sizeof(TrigCreateProps)              == 0x08, "props size (act_create_unit's `sub esp, 8h`)");
static_assert(offsetof(TrigCreateProps, dwValue)   == 0x00, "props::dwValue  [ebp-8h]");
static_assert(offsetof(TrigCreateProps, dwRegion)  == 0x04, "props::dwRegion [ebp-4h]");

typedef int (__stdcall *SetUnitPropCb)(CUnit *pUnit, int nValue);

// The kill/remove predicate -- `cond(nNation /*ecx*/, wUnitType /*dx*/,
// pUnit /*stack*/)`, __fastcall, `ret 4h`. Every `_Cond` body takes its unit
// from [ebp+8h] and compares ECX against CUnit::bPlayer, and
// TrigAct_KillRemoveType_Cond compares DX against CUnit::wType with
// `cmp word ptr [esi+64h], dx`, a 16-bit compare an int parameter cannot
// produce. The caller consumes the result with `test eax, eax`.
typedef int (__fastcall *KillRemoveCondCb)(DWORD nNation, WORD wUnitType,
                                           CUnit *pUnit);

// External callees, each called or address-taken by a landed body below.

// oracle 0x4B7750 (169 B). The per-unit visitor all six sweep handlers hand to
// find_foreach_unit: `int __fastcall(CUnit * /*ecx*/, void * /*edx*/)`, which is
// what src117/h/TrigEnum.h's FindUnitCb spells.
int __fastcall sub_4B7750(CUnit *pUnit, void *pCtx);

// The two spec callbacks this file does not define, address-taken by
// act_set_unit_hp and act_give. Only the type matters here, and it is
// SetUnitPropCb's.
int __stdcall set_health(CUnit *pUnit, int nValue);   // 0x4B8110
int __stdcall set_owner(CUnit *pUnit, int nValue);    // 0x4B81A0

// oracle 0x4B7C70 (135 B) and 0x4B73A0 (156 B), both TrigAct.cpp and both still
// unnamed in the ledger, so the `sub_` spelling is the symbol. They are the
// region-scoped and nation-scoped halves of one sweep:
//   sub_4B7C70  `ret 0Ch`. It spills ECX as a dword nation and DX as
//     `mov word ptr [ebp-10h], dx`, a 16-bit spill into a 16-bit home, which is
//     what types argument 2 a WORD. [ebp+8h] is the predicate, [ebp+0Ch] the
//     region (gMapRegions at the 20-byte stride), [ebp+10h] the limit.
//   sub_4B73A0  `ret 8h`: [ebp+8h] the predicate (`call dword ptr [ebp+8h]`)
//     and [ebp+0Ch] the limit, counted down. ECX is the nation, passed to
//     CUnit::GetPlayerUnitList.
// Both are declared void: the one call site discards EAX and neither body
// normalises it.
void __fastcall sub_4B7C70(DWORD nNation, WORD wUnitType, KillRemoveCondCb pfnCond,
                           DWORD nRegion, int nLimit);
void __fastcall sub_4B73A0(DWORD nNation, WORD wUnitType, KillRemoveCondCb pfnCond,
                           int nLimit);

// oracle 0x4B2EE0 (15 B), whole body:
//   `call request_statcmd_update / call request_statport_update /
//    jmp request_statdata_update`
// No register is read before being written and the tail jmp cleans nothing =>
// no arguments, void.
void __fastcall status_set_update();

// oracle 0x4BD460 (13 B) / 0x4BD470 (13 B) / 0x4BD5D0 (13 B). Each is reached by
// a bare `call` with NO argument set-up whatsoever -- no push, no ECX/EDX load --
// so each takes no arguments, and each caller ignores EAX.
void __fastcall CurrentTriggerDisableAbort(void);
void __fastcall CurrentTriggerEnableAbort(void);
void __fastcall KeepCurrentTrigger(void);

// More callees, ordered by oracle VA. Four names this TU reaches are declared
// elsewhere: do_minimap_ping in src117/h/minimap.h (a second declaration with
// different parameter types would be two real declarations of one name), the
// victory.h family (TriggerPlayer, IsCurrentTriggerAborted, the victory-code
// and delayed-action pairs, CurrentTriggerPaused / CurrentTriggerUnpaused,
// victory_force_check, sub_4BE3D0), CUnit::{Enable,Disable,Toggle}DoodadState
// in src117/h/CUnit.h, and SStrCopy and memset, which are imports declared in
// the .cpp.

// CUnitColor.cpp 0x41D7B0. Nullary -- act_alliance sets up no argument and
// ignores EAX.
void __fastcall update_unit_sel_colors(void);

// cursor.cpp 0x43E590 / 0x43E600. Nullary, EAX ignored, reached by a bare
// `call` with no argument set-up whatsoever.
void __fastcall cur_hide(void);
void __fastcall cur_show(void);

// accel.cpp 0x43F100 / 0x43F160 / 0x43F180. Nullary, same reading. The oracle's
// own VA order pairs them across the pause/unpause split rather than by name:
// act_pause calls accel_save then accel_set_sound, act_unpause calls only
// accel_restore.
void __fastcall accel_set_sound(void);
void __fastcall accel_restore(void);
void __fastcall accel_save(void);

// gamemap.cpp 0x445670. `mov cx, <id>` and nothing else, so one WORD register
// argument. The return is `const char *`: C++ mangles the return type into the
// decorated name and src117/lang/CUnitMisc.cpp:134 already declares this symbol
// with the const, which SStrCopy's and map_msg_default's source parameters want
// anyway.
const char *__fastcall get_map_string(WORD wStringId);

// gamesnd.cpp 0x446930 / 0x446FD0. Nullary: neither call site sets up an
// argument and neither reads EAX.
void __fastcall gamesnd_mute_speech(void);
void __fastcall gamesnd_unmute_speech(void);

// mapmsg.cpp 0x46AEC0. ECX = the string, EDX = the display time; act_text is
// the only call site in the wave and it pushes nothing.
void __fastcall map_msg_default(const char *pszText, int nTime);

// net_cmds.cpp 0x4721C0. `mov ecx, eax` off TriggerPlayer() -> one register
// argument, nothing pushed.
void __fastcall AllianceUpdateUnits(int nPlayer);

// CBullet.cpp 0x478200. ECX is a per-tag counter index, not a bound: the body
// opens `inc dword ptr [ecx*4+673940h]`. Result consumed by act_set_switch as
// a signed BYTE (`test al, al / js`), but the callee defines all of EAX.
int __fastcall net_rand(int nTag);

// net_glue.cpp 0x479580. One BYTE argument: act_pause and act_unpause both set
// only CL (`mov cl, byte ptr [eax+8h]` off gPlayerData[].nType) and the body's
// first act is a byte use. An int parameter would have forced a `movzx` at both
// call sites.
int __fastcall IsNamedComputerPlayerType(BYTE nType);

// game.cpp 0x47CBB0 / 0x47CC10. pause_game is nullary; unpause_game takes one
// register argument, and it must be declared or act_unpause loses the two-byte
// `xor ecx, ecx` that MSVC schedules ahead of the gbTriggerPaused store.
void __fastcall pause_game(void);
void __fastcall unpause_game(int bFlag);

// gametype.cpp 0x493970. `ret 4h` with ECX and EDX live: the 4-byte script id
// in ECX (compared against 0x75637352), the player in EDX, and the area at
// [ebp+8h], handed straight to aiscript_new_thread. The third parameter is
// `const rect32_t *` because src117/lang/gametype.cpp:194 already declares the
// symbol that way and C++ mangles the pointee into the name; MapRegion's first
// sixteen bytes are a rect32_t field for field, so act_run_ai_atloc's cast of
// `&gMapRegions[n]` is type plumbing and moves no code.
void __fastcall run_aiscript(DWORD dwScriptId, int nPlayer,
                             const rect32_t *prcArea);

// statport.cpp 0x4A7320. act_portrait is the only call site: ECX a WORD unit
// type, EDX a full dword -1, then two pushed dwords with no caller-side
// cleanup, so dwTime is last and the pushed -1 is third. The roles of the two
// -1s are named for position, not derived. BOOL: every path ends `mov eax, 1h`.
BOOL __fastcall show_portrait(WORD wUnitType, int a2, int a3, DWORD dwTime);

// The score / resource / death triples. Every caller is `mov ecx,[act+10h] /
// mov dx,[act+18h] / push [act+14h] / call`, so argument 2 is a WORD. The return
// is int because each body returns its enumerator's EAX on the group paths.
// Each is its own NationEnumCb, so the value rides in the context pointer.
int __fastcall add_deaths(DWORD nGroup, WORD wUnitType, void *pCtx);    // 0x4B6A90
int __fastcall add_resources(DWORD nGroup, WORD wUnitType, void *pCtx); // 0x4B6BE0
int __fastcall add_score(DWORD nGroup, WORD wUnitType, void *pCtx);     // 0x4B6CF0
int __fastcall set_deaths(DWORD nGroup, WORD wUnitType, void *pCtx);    // 0x4B7E80
int __fastcall set_resources(DWORD nGroup, WORD wUnitType, void *pCtx); // 0x4B82D0
int __fastcall set_score(DWORD nGroup, WORD wUnitType, void *pCtx);     // 0x4B8440
int __fastcall sub_deaths(DWORD nGroup, WORD wUnitType, void *pCtx);    // 0x4B8640
int __fastcall sub_resources(DWORD nGroup, WORD wUnitType, void *pCtx); // 0x4B8810
int __fastcall sub_score(DWORD nGroup, WORD wUnitType, void *pCtx);     // 0x4B8950

// TrigAct.cpp's own, unported. 0x4B6E90 (272 B). The same three-argument
// __fastcall shape as the triples above; act_alliance passes a literal 0 as the
// third. It sits after its caller in VA order, so in the .cpp this declaration
// is the forward declaration act_alliance (0x4B5E90) compiles against.
int __fastcall Trig_SetAlliance(DWORD nGroup, WORD wStatus, void *pCtx);

// TrigAct.cpp's own, unported. 0x4B7170 (114 B). `ret 4h`: the unit type in CX,
// the group in EDX, and the region as one pushed dword carrying a one-byte
// aggregate (TrigRegionId above).
CUnit *__fastcall TrigFindFirstUnitAt(WORD wUnitType, DWORD nGroup,
                                      TrigRegionId region);

// 0x4B71F0 (303 B). TrigFindFirstUnitAt's find_foreach_unit visitor, and its
// own recursion into subunits and passengers.
int __fastcall sub_4B71F0(CUnit *pUnit, void *pCtx);

// 0x4B7A80 (154 B). Moves a unit to (x, y) or back to where it was; `ret 4h`,
// x in DX and y on the stack.
void __fastcall sub_4B7A80(CUnit *pUnit, spos_t x, spos_t y);  // Hyperion: positions

// 0x4B7810 (618 B). The building form of sub_4B7A80: relocates a completed
// building on the placement grid; `ret 4h`, x in DX and y on the stack.
void __fastcall sub_4B7810(CUnit *pUnit, DWORD x, DWORD y);

// 0x4B7D00 (371 B). sub_4B7C70's find_foreach_unit visitor.
int __fastcall sub_4B7D00(CUnit *pUnit, void *pCtx);

// 0x4B7330 (112 B). `ret 8h`. Writes the first matching unit's sprite position,
// or the region's centre when there is none, and returns the unit. The region is
// a widened byte at both ends.
CUnit *__fastcall sub_4B7330(WORD wUnitType, DWORD dwGroup, BYTE bRegion,
                             TrigPoint *pPt);

// 0x4B7020 (271 B). `ret 4h`, and `xor eax, eax` on every exit.
int __fastcall Trig_CreateUnitWithProperties(DWORD dwGroup, WORD wUnitType,
                                             void *pCtx);

// The find_foreach_unit visitors of act_move_unit, act_invincible and act_order.
int __fastcall trigact_moveunit_cb(CUnit *pUnit, void *pCtx);          // 0x4B7B20
int __fastcall trigact_set_invincibility_cb(CUnit *pUnit, void *pCtx); // 0x4B7440
int __fastcall TrigOrderUnit(CUnit *pUnit, void *pCtx);                // 0x4B75D0, act_order's visitor
// Hyperion: one unit given act_order's move (0), patrol (1) or attack (2) toward x, y,
// as the action gives each unit it sweeps; nonzero when it took it (port_trigrun.cpp).
int PortTrigOrderOne(CUnit *pUnit, int x, int y, int nOrder);

// The landed bodies, in oracle VA order, which is this TU's declaration order:
// under /Ox a TU's functions land in .text in declaration order, and
// src117/lang/TrigAct.cpp keeps that sequence. The TU's other bodies have no
// slot here; they are unwritten rather than missing.

// 0x4B5620 (49 B). "Show portrait": the local-player gate feeding a
// four-argument show_portrait.
int __fastcall act_portrait(TrigAction *pAction);

// 0x4B5660 (33 B). "Mute unit speech" -- the local-player gate with the abort
// test. act_unmute below has none; the asymmetry is the oracle's.
int __fastcall act_mute(void);

// 0x4B5690 (24 B). "Unmute unit speech" -- the same gate without the abort test.
int __fastcall act_unmute(void);

// 0x4B56B0 (59 B) and 0x4B67C0 (115 B). The leaderboard actions, all players
// and computer players: bCount 4/5/6 set, clear or toggle one dword flag. They
// are declared here rather than in replay.cpp because replay_lb_show and
// replay_lb_hide call the all-players one.
int __fastcall act_leaderboard(TrigAction *pAction);
int __fastcall act_leaderboard_cpu(TrigAction *pAction);

// 0x4B5840 (185 B). "Move unit": sweep the region and hand each match to
// trigact_moveunit_cb.
int __fastcall act_move_unit(TrigAction *pAction);

// 0x4B56F0 (322 B). "Move location": recentre a location on the unit it
// names, clamped to the map, with the deltas computed before the four adds.
int __fastcall act_move_loc(TrigAction *pAction);

// 0x4B5900 (34 B). "Load next mission": SStrCopy the action's map string into
// the 32-byte next-mission name buffer. The guard is a DWORD compare on the
// whole of dwStringId and the call then narrows the same slot to a WORD, which
// is what types that field.
int __fastcall act_next_mission(TrigAction *pAction);

// 0x4B5930 (108 B). "Set doodad state": set, clear or toggle the doodad state
// of the first matching unit in the action's map region. Its third argument to
// TrigFindFirstUnitAt is a TrigRegionId.
int __fastcall act_doodad(TrigAction *pAction);

// 0x4B59C0 (139 B). "Set invincibility" over a region.
int __fastcall act_invincible(TrigAction *pAction);

// 0x4B5A50 (173 B). "Give units to player": sweep the action's map region and
// hand every matching unit to set_owner.
int __fastcall act_give(TrigAction *pAction);

// 0x4B5B00 (173 B). "Set unit hit points" -- a percentage, applied by set_health.
int __fastcall act_set_unit_hp(TrigAction *pAction);

// 0x4B5BB0 (173 B). "Set unit energy points", applied by set_energy.
int __fastcall act_set_unit_energy(TrigAction *pAction);

// 0x4B5C60 (173 B). "Set unit shield points", applied by set_shields.
int __fastcall act_set_unit_shields(TrigAction *pAction);

// 0x4B5D10 (174 B). "Set resource amount on units", the one member of the
// sextet that is not a callback swap: its unit-type filter is the literal 0xE5
// rather than the action record's own wUnitType.
int __fastcall act_set_unit_res(TrigAction *pAction);

// 0x4B5DC0 (173 B). "Set hangar count" (interceptors / scarabs), applied by
// set_hangar.
int __fastcall act_set_hangar(TrigAction *pAction);

// 0x4B5E70 (16 B). Whole body: `mov dword ptr [53632Ch], 1h / mov eax, 1h /
// ret`. Pauses the trigger countdown timer. The oracle puts act_stop_timer
// before act_start_timer (0x4B5E80) even though the names suggest the reverse;
// VA order is the authority.
int __fastcall act_stop_timer(void);

// 0x4B5E80 (16 B). The sibling, storing 0 where act_stop_timer stores 1. Two
// writers storing 0 and 1 to one dword is what types that global.
int __fastcall act_start_timer(void);

// 0x4B5E90 (50 B). "Set alliance status". TriggerPlayer() is called twice and
// the oracle does not cache it -- once for AllianceUpdateUnits, once for the
// local-player gate. The gate carries no abort test, matching act_unmute.
int __fastcall act_alliance(TrigAction *pAction);

// 0x4B5ED0 (11 B) / 0x4B5EE0 (11 B). Two one-call leaves. act_preserve
// (0x4B5FC0) is a third and sits further down, after the victory trio.
int __fastcall act_disable_escape(void);
int __fastcall act_enable_escape(void);

// 0x4B5EF0 (45 B) / 0x4B5F20 (80 B) / 0x4B5F70 (80 B). The victory / defeat /
// draw trio: each writes the nation's victory code and then either
// victory_force_check (when a trigger has the game paused) or sub_4BE3D0.
// act_victory has no gate; defeat and draw share a two-test one over the
// current code (3 and 4 are terminal and both bail with `return 0`), and differ
// only in the code they store, 2 against 5.
int __fastcall act_victory(void);
int __fastcall act_defeat(void);
int __fastcall act_draw(void);

// 0x4B5FC0 (11 B). The third one-call leaf.
int __fastcall act_preserve(void);

// 0x4B5FD0 (70 B). "Wait" -- the one handler in this file that returns 0 on a
// live path, which means stop executing this trigger's action list for now: a
// pending delay returns 0, a set bFlags bit 1 means the wait just finished and
// returns 1 after clearing it, otherwise it arms the timer, sets the bit and
// returns 0.
int __fastcall act_wait(TrigAction *pAction);

// 0x4B6020 (89 B) / 0x4B6080 (90 B). "Pause game" / "Unpause game", and the
// guards are not mirror images. act_pause has four conditions (single-player,
// not already trigger-paused, a non-computer trigger player, and not aborted);
// act_unpause has three -- it requires the paused state rather than its
// absence, and has no abort gate, which it must not have, or an abort during a
// paused trigger would strand the game paused.
int __fastcall act_pause(void);
int __fastcall act_unpause(void);

// 0x4B60E0 (342 B). "Transmission": ping and portrait the sender, then hold the
// trigger for the message's duration.
int __fastcall act_transmission(TrigAction *pAction);

// 0x4B6240 (180 B). "Play WAV": the briefing body, reached by
// act_transmission as well as by the dispatch table.
int __fastcall act_wav(TrigAction *pAction);

// 0x4B6300 (77 B). "Display text message". Four guards, and the middle two are
// a short-circuit or: the options bit being set bypasses the per-action flag
// test rather than adding to it, which is visible only in the branch
// displacements.
int __fastcall act_text(TrigAction *pAction);

// 0x4B64D0 (118 B). "Create unit with properties".
int __fastcall act_create_unit(TrigAction *pAction);

// 0x4B6350 (372 B). "Center view".
int __fastcall act_center_view(TrigAction *pAction);

// 0x4B6550 (78 B) / 0x4B6940 (78 B) / 0x4B6990 (78 B). The death / resource /
// score triples -- one shape three times, bCount 7/8/9 selecting set, add or
// subtract and differing only in the three callees. act_set_resource and
// act_set_score sit further down in VA order, after the kill/remove pair; they
// are named here because the three are one family.
int __fastcall act_set_deaths(TrigAction *pAction);

// 0x4B65A0 (201 B). "Order": sweep the action's units and issue the order.
int __fastcall act_order(TrigAction *pAction);

// 0x4B6670 (25 B). "Set mission objectives": store the whole dword of
// dwStringId into the per-nation table, indexed by TriggerPlayer().
int __fastcall act_mission_obj(TrigAction *pAction);

// 0x4B6690 (130 B). "Set switch" -- a bit write into the 256-switch bitmap.
// bCount 4/5/6 set, clear or toggle, and 0x0B randomises; the randomise arm
// reuses the blocks of cases 4 and 5, so the body carries two gotos and keeps
// 0x0B out of the switch.
int __fastcall act_set_switch(TrigAction *pAction);

// 0x4B6720 (61 B). "Set countdown timer", clamped to 0x57E3F.
int __fastcall act_set_timer(TrigAction *pAction);

// 0x4B6760 (27 B) / 0x4B6780 (51 B). "Run AI script" and its at-location
// sibling. The first passes a NULL area; the second runs the two-step region
// guard and passes &gMapRegions[n].
int __fastcall act_run_ai(TrigAction *pAction);
int __fastcall act_run_ai_atloc(TrigAction *pAction);

// 0x4B6840 (43 B). "Kill unit", the whole-nation form.
int __fastcall act_kill(TrigAction *pAction);

// 0x4B6870 (65 B) / 0x4B68F0 (65 B). "Kill unit at location" / "Remove unit at
// location" -- byte-for-byte identical bar the immediate they store into
// sgbRemoveUnit_g, which is the whole difference between killing and removing.
int __fastcall act_kill_atloc(TrigAction *pAct);
int __fastcall act_remove_atloc(TrigAction *pAct);

// 0x4B68C0 (43 B). "Remove unit", act_kill's twin.
int __fastcall act_remove(TrigAction *pAction);

// 0x4B6940 (78 B) / 0x4B6990 (78 B). The other two of the triple family above.
int __fastcall act_set_resource(TrigAction *pAction);
int __fastcall act_set_score(TrigAction *pAction);

// 0x4B69E0 (103 B). "Minimap ping": flash the minimap at a location's centre.
// Three guards, all exiting to one tail; the centre arithmetic is a signed
// divide by two, which is what types MapRegion's bounds int.
int __fastcall act_ping(TrigAction *pAction);

// 0x4B6A50 (6 B). Whole body: `mov eax, 6C4700h / ret` -- the base of the
// unit-properties buffer resetUnitProps zeroes.
BYTE *__fastcall getUnitProps(void);

// 0x4B6A70 (21 B). Zero that buffer: `push 500h / push 0h / push 6C4700h /
// call memset / add esp, 0Ch / ret`. No prologue, no arguments, and EAX falls
// straight out of memset's return value, which is what makes this void.
void __fastcall resetUnitProps(void);

// 0x4B6FE0 (61 B). The display time act_text hands to map_msg_default: 50 ms
// per character, floored at 4000. It sits after its caller (0x4B6300) in VA
// order, so in the .cpp this declaration is act_text's forward declaration.
int __fastcall Trig_GetTextDisplayTime(const char *pszText);

// 0x4B7FD0 (122 B). SetUnitPropCb. Sets energy to a percentage of the unit's
// maximum, and reproduces a real 1.17 defect, an 8-bit truncation of the
// percentage result (docs/open-issues.md and the body's own note).
int __stdcall set_energy(CUnit *pUnit, int nValue);

// 0x4B8050 (184 B). SetUnitPropCb. Fills a carrier's or reaver's hangar by
// creating nValue fighters outright.
int __stdcall set_hangar(CUnit *pUnit, int nValue);

// 0x4B82C0 (9 B). Not a SetUnitPropCb but a different ABI. Whole body:
// `push edx / call CUnit::GiveToPlayer / xor eax,eax / ret`, a bare `ret` with
// ECX and EDX consumed without ever being loaded: __fastcall with two register
// arguments. It is the per-cargo-unit visitor set_owner (0x4B81A0) hands to
// CUnit::IterateTransport and CUnit::FighterDoFcn, so it does not belong in a
// SetUnitPropCb table.
int __fastcall set_owner_inside(CUnit *pUnit, CUnit *pContext);  // the player rides in pContext

// 0x4B83E0 (81 B). SetUnitPropCb. Sets a resource container's amount and
// refreshes the mineral-patch animation.
int __stdcall set_resources_unit(CUnit *pUnit, int nValue);

// 0x4B85E0 (85 B). SetUnitPropCb. Sets shields to a percentage of the unit type's
// maximum, gated on gbUnitHasShields.
int __stdcall set_shields(CUnit *pUnit, int nValue);

// 0x4B8C40 (21 B) / 0x4B8C60 (42 B) / 0x4B8C90 (42 B) / 0x4B8E10 (42 B) /
// 0x4B8E40 (38 B). The five KillRemoveCondCb predicates. Four ignore argument
// 2, which must still be declared, because all five are assigned to one
// variable in TrigAct_KillRemoveUnits and share its type.
int __fastcall TrigAct_KillRemoveAnyUnit_Cond(DWORD nNation, WORD wUnitType,
                                              CUnit *pUnit);
int __fastcall TrigAct_KillRemoveBuildings_Cond(DWORD nNation, WORD wUnitType,
                                                CUnit *pUnit);
int __fastcall TrigAct_KillRemoveFactories_Cond(DWORD nNation, WORD wUnitType,
                                                CUnit *pUnit);

// 0x4B8CC0 (256 B). The dispatcher, and its own callback: for a pseudo-group it
// hands its own address to the matching *_enum_count, which re-enters it once
// per concrete nation; for a concrete nation it picks one of the five
// predicates by unit-type code and runs the sweep. `ret 8h`, two register and
// two stack arguments. nRegion == 0xFF is the "no location, sweep the whole
// nation" sentinel, which is what the two sub_ helpers are for.
int __fastcall TrigAct_KillRemoveUnits(DWORD nGroup, WORD wUnitType,
                                       DWORD_PTR nRegion, int nLimit);

int __fastcall TrigAct_KillRemoveMen_Cond(DWORD nNation, WORD wUnitType,
                                          CUnit *pUnit);
int __fastcall TrigAct_KillRemoveType_Cond(DWORD nNation, WORD wUnitType,
                                           CUnit *pUnit);

// 0x4B8E70 (6 B). `mov eax, 1h / ret` -- the no-op action handler.
int __fastcall act_none(void);

// 0x4B6A60 (8 B). Forwards both register arguments to set_owner; EAX is only
// inherited and the one caller, CUnit::sub_430F60, discards it.
void __fastcall sub_4B6A60(CUnit *pUnit, int nPlayer);
