// TAI_Captain.h -- the TAI_Captain.cpp entry points src117 code calls: the
// per-unit TSoldier record and its pool, the per-area TAI_Captain record, and
// the captain, soldier and aiscript functions over them. The bodies are in
// src117/lang/TAI_Captain.cpp.
//
// Free functions are spelled without an explicit __fastcall: /Gr already makes
// it the default.
#pragma once

#include "platform.h"
#include <stddef.h>   // offsetof -- the layout static_asserts below

#include "point16.h"
#include "rect16.h"   // aiscript_assign_all_order takes one BY VALUE
#include "rect32.h"
#include "aipools.h"  // Hyperion: AI_RECORDS_STORE

class CUnit;
struct TAI_Captain;

// TSoldier -- the per-unit AI record, 20 B. CUnit::dwUnion134 points at one.
// The array is gSoldiers_g @0x6ACC50 and the ids are 1-based: soldier_ptr
// biases the base by one element (0x6ACC50 - 20 == 0x6ACC3C) and soldier_id
// adds one back. The name is the ledger's, off the mangled
// TSaveNode<TSoldier,1000> rows at 0x4A9A90 and 0x4A9AC0, which also give the
// count: 1000 * 20 == 0x4E20, and 0x6ACC50 + 0x4E20 == 0x6B1A70, four bytes
// short of the next named global.
struct TSoldier {
    /*0x00*/ TSoldier    *pNext;
    // +0x04 is the prev link: tai_captain_unlink_soldier's unlink runs
    // prev->next = next and next->prev = prev over +0x00 and +0x04.
    /*0x04*/ TSoldier    *pPrev;
    /*0x08*/ BYTE         bType;
    /*0x09*/ BYTE         bUnknown09;
    // +0x0A is the soldier's cached region, a WORD: tai_soldier_issue_order
    // stores GetRegion_CUnit's WORD return here with
    // `mov word ptr [edi+0Ah], ax`. A captain keeps its region at the same
    // offset, so a read of either has to say which record the register holds.
    /*0x0A*/ WORD         wRegion;
    /*0x0C*/ CUnit       *pUnit;
    /*0x10*/ TAI_Captain *pCaptain;
};
static_assert(offsetof(TSoldier, pNext)    == 0x00, "TSoldier.pNext    [eax]");
OC_X86_LAYOUT(offsetof(TSoldier, bType)    == 0x08, "TSoldier.bType    [edx+8h] BYTE");
OC_X86_LAYOUT(offsetof(TSoldier, pUnit)    == 0x0C, "TSoldier.pUnit    [eax+0Ch]");
OC_X86_LAYOUT(offsetof(TSoldier, pCaptain) == 0x10, "TSoldier.pCaptain [eax+10h]");
OC_X86_LAYOUT(offsetof(TSoldier, pPrev)    == 0x04, "TSoldier.pPrev    [edx+4h]");
OC_X86_LAYOUT(offsetof(TSoldier, wRegion)  == 0x0A, "TSoldier.wRegion  [edi+0Ah] WORD");
OC_X86_LAYOUT(sizeof(TSoldier) == 0x14, "TSoldier stride (soldier_ptr x20 / soldier_id /20)");

// The soldier pool a captain draws from; TAI_Captain +0x2C points at one. The
// free-list head sits immediately past the array, at +0x4E20 == 1000 *
// sizeof(TSoldier), and the object spans 0x4E24 bytes: gSoldiers_g @0x6ACC50
// plus 0x4E20 is 0x6B1A70, and the next named global, sgCaptain, is at
// 0x6B1A74. The pool is spelled as the ledger spells it,
// TSaveNode<TSoldier,1000>; TSoldierPool is an alias.

template <class T, int N> struct TSaveNode;
template <> struct TSaveNode<TSoldier,1000> {
    /*0x0000*/ TSoldier  aSoldiers[AI_RECORDS_STORE];  // Hyperion: 1000 in 1.17
    /*0x4E20*/ TSoldier *pFreeHead;

    void Init();
    void PreSave();
    void PostLoad();
};
typedef TSaveNode<TSoldier,1000> TSoldierPool;
OC_X86_LAYOUT(offsetof(TSoldierPool, pFreeHead) == 0x4E20,
              "TSoldierPool.pFreeHead [ecx+4E20h]");
OC_X86_LAYOUT(sizeof(TSoldierPool) == 0x4E24, "TSoldierPool = TSoldier[1000] + head");

// A captain's job (TAI_Captain::bJob), as tai_captain_service's table
// dispatches it and sai_soldier_set_type sets it. The names are the port's,
// read off the job handlers' bodies; the 1.16.1 names of the handlers for 7,
// and for 8 and 9, agree.
enum CaptainJob {
  CAPTAIN_JOB_IDLE = 0,  // No job
  CAPTAIN_JOB_ATTACK = 1,  // An attack group on the move
  CAPTAIN_JOB_ATTACK_ENGAGED = 2,  // The same handler as job 1, entered when the group passes twenty ground soldiers or one of its units is hit, in tai_unit_was_attacked
  CAPTAIN_JOB_LOST = 3,  // The region is lost: set by TAI_Captain_RemoveUnit when a soldier dies away from home and by tai_captain_disband_if_weak
  CAPTAIN_JOB_DEFEND = 4,  // Defend an approach: set by capt_expand, implode, help_iftrouble, tai_captain_claim_defence_region and tai_captain_defend_paths_to_enemies
  CAPTAIN_JOB_HOLD = 5,  // Defend something worth holding: set by value_area and guard, and by tai_captain_job_under_attack and tai_captain_job_reinforce when they settle
  CAPTAIN_JOB_UNDER_ATTACK = 6,  // A threat is standing in the region
  CAPTAIN_JOB_REINFORCE = 7,  // Build up and then escort everything to the region in wUnknown02
  CAPTAIN_JOB_GATHER = 8,  // Muster for the attack the script ordered: set by tai_captain_choose_attack_target on the staging region with the target in wUnknown02, and by tai_captain_start_expansion
  CAPTAIN_JOB_REGATHER = 9,  // The same handler as job 8, set by the give-up paths of tai_captain_job_gather and tai_captain_job_defend on the region of a newly found target, with no hand-off region
};

// TAI_Captain -- the per-area AI captain record, `sgCaptain[nation][area]`,
// stride 0x34 (`imul e?x, <area>, 34h` at sixty-odd oracle sites). Each field
// below is read off an instruction that fixes its width; the rest is
// `_unmapped` on purpose, and naming one needs the instruction that reads it.
struct TAI_Captain {
    // +0x00 is a WORD, not a DWORD: three bodies compare it against
    // GetRegion_CUnit's WORD return with a 16-bit `cmp ax, word ptr [esi]`,
    // where a DWORD field gives `movzx eax, ax / cmp eax, [esi]` instead.
    /*0x00*/ WORD      wRegion;
    /*0x02*/ WORD      wUnknown02;
    // +0x04 is the nation, not the area: captain_ptr_to_id uses it as the first
    // sgCaptain index (`movzx ecx, byte ptr [edi+4h]` /
    // `sub edi, dword ptr [ecx*4+6B1A74h]`, then `imul eax, ecx, 9C4h`).
    /*0x04*/ BYTE      bNation;
    /*0x05*/ BYTE      bJob;
    /*0x06*/ BYTE      bUnknown06;
    /*0x07*/ BYTE      bTimer;
    /*0x08*/ BYTE      bFlags;
    /*0x09*/ BYTE      bUnknown09;
    /*0x0A*/ WORD      wUnknown0A;
    /*0x0C*/ WORD      wUnknown0C;
    /*0x0E*/ WORD      wUnknown0E;
    // Four WORD accumulators, filled in one breath by
    // tai_captain_add_unit_strength_cb: CUnit::AirStrength into +0x16 and
    // +0x12, CUnit::GroundStrength into +0x14 and +0x10. Which callee fed AX is
    // the air and ground split; what separates +0x12 from +0x16, or +0x10 from
    // +0x14, is not read anywhere, so the offset stays in the name.
    /*0x10*/ WORD      wGroundStr10;
    /*0x12*/ WORD      wAirStr12;
    /*0x14*/ WORD      wGroundStr14;
    /*0x16*/ WORD      wAirStr16;
    // Two more WORD accumulators: tai_captain_region_enemy_strength reads both,
    // and tai_captain_recompute_threats picks between them on CUnit::dwFlags
    // & 4 as +0x10..+0x16 do.
    /*0x18*/ WORD      wUnknown18;
    /*0x1A*/ WORD      wUnknown1A;
    /*0x1C*/ CUnit    *pUnit1C;
    /*0x20*/ CUnit    *pUnit20;
    // +0x24 is a `CUnit *`: tai_captain_pick_leader loads it, tests
    // `byte ptr [eax+0DCh]` (CUnit::dwFlags) and reads +0x34 (lTopSpeed), then
    // stores a CUnit back into it.
    /*0x24*/ CUnit    *pUnit24;
    // +0x28 is also a `CUnit *`: tai_captain_take_patroller tests it for null
    // and stores the incoming unit, which it reads at +0x4C for bPlayer.
    /*0x28*/ CUnit    *pUnit28;
    // +0x2C is the captain's soldier pool: `mov ecx, [ecx+2Ch]` then
    // `mov eax, [ecx+4E20h]` in tai_captain_unlink_soldier.
    /*0x2C*/ TSoldierPool *pPool;
    /*0x30*/ TSoldier *pSoldiers;
};
static_assert(offsetof(TAI_Captain, wRegion)     == 0x00, "TAI_Captain.wRegion    [esi]     WORD");
static_assert(offsetof(TAI_Captain, bNation)    == 0x04, "TAI_Captain.bNation    [esi+4h]  BYTE");
static_assert(offsetof(TAI_Captain, wGroundStr10) == 0x10, "TAI_Captain.wGroundStr10 [esi+10h] WORD");
static_assert(offsetof(TAI_Captain, wAirStr12)  == 0x12, "TAI_Captain.wAirStr12  [esi+12h] WORD");
static_assert(offsetof(TAI_Captain, wGroundStr14) == 0x14, "TAI_Captain.wGroundStr14 [esi+14h] WORD");
static_assert(offsetof(TAI_Captain, wAirStr16)  == 0x16, "TAI_Captain.wAirStr16  [esi+16h] WORD");
OC_X86_LAYOUT(offsetof(TAI_Captain, pPool)       == 0x2C, "TAI_Captain.pPool      [ecx+2Ch]");
OC_X86_LAYOUT(offsetof(TAI_Captain, pUnit24)     == 0x24, "TAI_Captain.pUnit24    [eax+24h]");
OC_X86_LAYOUT(offsetof(TAI_Captain, pUnit28)     == 0x28, "TAI_Captain.pUnit28    [ecx+28h]");
static_assert(offsetof(TAI_Captain, bJob)       == 0x05, "TAI_Captain.bJob       [eax+5h] BYTE");
static_assert(offsetof(TAI_Captain, bTimer)     == 0x07, "TAI_Captain.bTimer     [eax+7h] BYTE");
static_assert(offsetof(TAI_Captain, bFlags)     == 0x08, "TAI_Captain.bFlags     [eax+8h] BYTE");
static_assert(offsetof(TAI_Captain, wUnknown0A) == 0x0A, "TAI_Captain.wUnknown0A [ecx+0Ah] WORD");
static_assert(offsetof(TAI_Captain, wUnknown0C) == 0x0C, "TAI_Captain.wUnknown0C [esi+0Ch] WORD");
static_assert(offsetof(TAI_Captain, wUnknown0E) == 0x0E, "TAI_Captain.wUnknown0E [esi+0Eh] WORD");
OC_X86_LAYOUT(offsetof(TAI_Captain, pUnit1C)    == 0x1C, "TAI_Captain.pUnit1C    [esi+1Ch]");
OC_X86_LAYOUT(offsetof(TAI_Captain, pUnit20)    == 0x20, "TAI_Captain.pUnit20    [esi+20h]");
OC_X86_LAYOUT(offsetof(TAI_Captain, pSoldiers)  == 0x30, "TAI_Captain.pSoldiers  [ecx+30h]");
static_assert(offsetof(TAI_Captain, wUnknown18) == 0x18, "TAI_Captain.wUnknown18 [edx+18h] WORD");
static_assert(offsetof(TAI_Captain, wUnknown1A) == 0x1A, "TAI_Captain.wUnknown1A [edx+1Ah] WORD");
OC_X86_LAYOUT(sizeof(TAI_Captain) == 0x34, "TAI_Captain stride (imul e?x, <area>, 34h)");


// tai_soldier_captain_is_attacking -- 0x4B0580, 51 B. The archaeology sources
// do not name it, so the name describes the body only. Two register arguments:
// ECX is a CUnit *, dereferenced at once at +0x134 (dwUnion134), and EDX is
// read before it is written. EDX's width is unfalsifiable -- its only use is
// `test edx, edx` -- so it is spelled int. BOOL: every exit materialises 0
// or 1.
BOOL tai_soldier_captain_is_attacking(CUnit *pUnit, int nArg1);
// tai_soldier_unload_into_neighbor -- 0x4AF810, 238 B. Moved here from
// `class CUnit`; the reasoning is in the note it left at src117/h/CUnit.h. One
// register argument, a CUnit * by the +0x4C dereference. void: EAX at the ret
// is inherited from AIMoveWhereNeeded or CUnit::SetOrder_xy, and its single
// caller, CUnit::DispatchUnload, discards it.
void tai_soldier_unload_into_neighbor(CUnit *pUnit);
// 0x4A9A30 tai_captain_unlink_soldier (94 B). Two register arguments.
// `noinline` is for tai_captain_disband_if_weak's sake, not this body's: at
// 94 B /Ob2 splices the whole unlink into that loop.
__declspec(noinline) void tai_captain_unlink_soldier(TAI_Captain *pCaptain, TSoldier *pSoldier);
// 0x4AA2C0 tai_captain_add_unit_strength_cb (68 B). Two register arguments. A
// callback: its address is taken and it has no direct call site, so the arity
// is a lower bound and a register it ignores is invisible here.
// arg1 is a TAI_Captain *: the body dereferences EDX at +0x04 (vs
// CUnit::bPlayer), +0x00 (the WORD region) and the four +0x10..+0x16
// accumulators. Byte-invisible; a pointer is 4 bytes in a register.
BOOL tai_captain_add_unit_strength_cb(CUnit *pUnit, TAI_Captain *pCaptain);
// 0x4AA310 captains_alloc (63 B). One register argument.
void captains_alloc(int nArg0);
// 0x4AA4C0 tai_count_marine_defence (40 B). One register argument.
// The one caller, TAI_Captain_aiscript_should_rush, compares the result
// unsigned (`ja`).
unsigned tai_count_marine_defence(int nArg0);
// 0x4AA4F0 tai_unit_is_firebat (11 B). One register argument. A callback: its
// address is taken and it has no direct call site, so the arity is a lower
// bound and a register it ignores is invisible here.
BOOL tai_unit_is_firebat(CUnit *pUnit);
// 0x4AA8A0 tai_has_transports (76 B). One register argument.
BOOL tai_has_transports(int nArg0);
// 0x4AAAA0 tai_attack_helpless_cb (117 B). Two register arguments. A callback:
// its address is taken and it has no direct call site, so the arity is a lower
// bound and a register it ignores is invisible here.
BOOL tai_attack_helpless_cb(CUnit *pUnit, CUnit *pUnit2);
// 0x4AAB20 tai_captain_attack_map_spread_front (111 B). One register argument.
// arg0 is a BYTE * over the 0x9C4 area-state table: the body copies 0x9C4
// bytes (`mov ecx, 271h` + `rep movsd`) and bounds its loop `cmp esi, 9C4h`.
// 0x9C4 is the same 2500 captain_ptr_to_id biases the id with, so the areas per
// nation are fixed from two directions.
void tai_captain_attack_map_spread_front(BYTE *pStates);
// 0x4AAB90 tai_captain_attack_map_spread_own (137 B). Two register arguments,
// the first ignored. `int` is a placeholder for the ignored one.
// Both arguments are BYTE * over the same area-state table as
// tai_captain_attack_map_spread_front. ECX is the nation: its one caller,
// TAI_Captain_aiscript_attack, passes it there.
int tai_captain_attack_map_spread_own(int nNation, BYTE *pStates);
// 0x4AADC0 tai_unit_is_boardable_transport_cb (48 B). Two register arguments.
// A callback: its address is taken and it has no direct call site, so the
// arity is a lower bound and a register it ignores is invisible here.
BOOL tai_unit_is_boardable_transport_cb(CUnit *pUnit, CUnit *pUnit2);
// 0x4AAEB0 captain_free (51 B). Nullary.
void captain_free();
// 0x4AAEF0 tai_captain_survey_weapons (119 B). Two register arguments and two
// stack words (`ret 8h`).
// All three trailing arguments are `int *` out-params; arg1 is only ever
// written (`mov ebx, edx` / `mov dword ptr [ebx], 0h`). arg0 is a
// TAI_Captain * -- `mov esi, [ecx+30h]` is the soldier list, then an
// [esi]/[esi+0Ch] walk.
void tai_captain_survey_weapons(TAI_Captain *pCaptain, int *pAny, int *pAllGnd, int *pAllAir);
// 0x4AB0F0 tai_captain_job_is_gather (20 B). One register argument.
BOOL tai_captain_job_is_gather(TAI_Captain *pCaptain);
// 0x4AB110 tai_captain_has_forming_soldier (60 B). One register argument.
BOOL tai_captain_has_forming_soldier(TAI_Captain *pCaptain);
// 0x4AB150 tai_captain_gather_still_moving (117 B). One register argument. ECX
// is only forwarded to a callee, so this body shows no width for it.
BOOL tai_captain_gather_still_moving(int nArg0);
// 0x4AB2F0 tai_captain_regions_connected_xy (50 B). Two register arguments and
// three stack words (`ret Ch`).
int tai_captain_regions_connected_xy(int nArg0, int nArg1, int nArg2, int nArg3, int nArg4);
// 0x4AB420 tai_captain_units_connected (39 B). Two register arguments.
// arg1 is a CUnit *: `mov ecx, edx / call GetRegion_CUnit`, and
// CUnitSearches.h already types that parameter `CUnit *`.
int tai_captain_units_connected(CUnit *pUnit, CUnit *pUnit2);
// 0x4AB5D0 tai_captain_any_attack_job_active (76 B). One register argument.
BOOL tai_captain_any_attack_job_active(int nArg0);
// 0x4AB630 tai_soldier_enter_bunker_in_region (147 B). Two register arguments.
BOOL tai_soldier_enter_bunker_in_region(CUnit *pUnit, int nArg1);
// 0x4AB810 tai_captain_region_or_neighbor_held (85 B). Two register arguments.
BOOL tai_captain_region_or_neighbor_held(int nArg0, int nArg1);
// 0x4AB870 tai_captain_pick_leader (139 B). One register argument.
// The argument is a TAI_Captain *, not a CUnit *: `movzx ecx, byte ptr
// [eax+4h]` goes to aiscript_has_campaign as a nation, and `mov eax,
// [ecx+30h]` is walked as a TSoldier list. A CUnit's +0x04 is a dword pNext
// and is never read as a byte.
void tai_captain_pick_leader(TAI_Captain *pCaptain);
// 0x4AC080 tai_captain_decay_need (78 B). Two register arguments.
// The pointee is a WORD, from the callee side: the first instruction is
// `movzx eax, word ptr [ecx]` and every store is `mov word ptr [ecx], ax`,
// which corroborates TAI_Captain +0x0C and +0x0E as two WORDs. The width is
// also what makes the body compare with `mov edx, imm32 / cmp ax, dx`, which
// only a `const WORD` local reaches.
void tai_captain_decay_need(WORD *pw, int nArg1);
// 0x4AC740 tai_captain_job_defend (116 B). One register argument.
void tai_captain_job_defend(TAI_Captain *pCaptain);
// 0x4AC9B0 tai_captain_job_idle (65 B). One register argument.
void tai_captain_job_idle(TAI_Captain *pCaptain);
// 0x4AD2A0 TAI_Captain_aiscript_clear_combat_data (108 B). Two register
// arguments.
void TAI_Captain_aiscript_clear_combat_data(int nArg0, rect32_t *pArea);
// 0x4AD9A0 soldier_id (27 B). One register argument.
int soldier_id(TSoldier *pSoldier);
// 0x4AD9C0 soldier_ptr (18 B). One register argument.
TSoldier *soldier_ptr(int nSoldierId);
// 0x4AD9E0 TAI_Captain_aiscript_value_area (81 B). Two register arguments and
// one stack word (`ret 4h`).
void TAI_Captain_aiscript_value_area(int nArg0, int nArg1, int nArg2);
// 0x4ADA40 tai_captain_enlist_guard_cb (74 B). Two register arguments. A
// callback: its address is taken and it has no direct call site, so the arity
// is a lower bound and a register it ignores is invisible here.
// arg1 is a TAI_Captain *: same +0x04 / +0x00 dereferences, and it is then
// forwarded as ECX into tai_soldier_add_to_captain, whose first argument is
// one.
BOOL tai_captain_enlist_guard_cb(CUnit *pUnit, TAI_Captain *pCaptain);
// 0x4ADA90 tai_soldier_issue_order (120 B). Two register arguments.
void tai_soldier_issue_order(CUnit *pUnit, int nArg1);
// 0x4ADB10 tai_collect_pair (42 B). Two register arguments and two stack words
// (`ret 8h`).
BOOL tai_collect_pair(void *pArg0, void *pArg1, void *pArg2, intptr_t nArg3);
// 0x4ADB40 IsAiUsableSpellOrder (74 B). One register argument.
BOOL IsAiUsableSpellOrder(int nArg0);
// 0x4ADB90 tai_count_zerg_air_defence (54 B). One register argument.
// The one caller, TAI_Captain_aiscript_should_rush, compares the result
// unsigned (`ja`).
unsigned tai_count_zerg_air_defence(int nArg0);
// 0x4ADDE0 tai_count_zerg_ground_defence (36 B). One register argument.
// The one caller, TAI_Captain_aiscript_should_rush, compares the result
// unsigned (`ja`).
unsigned tai_count_zerg_ground_defence(int nArg0);
// 0x4ADF80 tai_captain_take_patroller (157 B). One register argument.
// arg0 is a CUnit *: `movzx ecx, byte ptr [edi+4Ch]` twice (bPlayer), and it
// is handed to CUnit::SetDestination_xy.
int tai_captain_take_patroller(CUnit *pUnit);
// 0x4AE040 tai_soldier_may_pick_own_target (49 B). One register argument.
BOOL tai_soldier_may_pick_own_target(CUnit *pUnit);
// 0x4AE380 tai_captain_take_bunker_site (85 B). Two register arguments.
// arg1 is a point16_t * out-param: `mov edx, ebx / call GetRegionPosition`,
// and sai.h types that parameter `point16_t *`. Twin of
// tai_captain_take_turret_site, mask 2 against 10h.
BOOL tai_captain_take_bunker_site(int nNation, mpoint_t *pPos);  // Hyperion: a map point
// 0x4AE700 tai_captain_neighbors_calm (95 B)
//   `ret`; EDX read before written -> a second register argument
//     ECX is read as 32 bits
//     EDX is read as 32 bits
//   no stack parameters
//   returns: 1 consuming caller(s) use the full EAX and none narrows it -> BOOL (every ret materialises 0 or 1)
BOOL tai_captain_neighbors_calm(int nArg0, int nArg1);
// 0x4AE840 tai_soldier_captain_air_threat (45 B). One register argument.
// It returns a value, although every caller discards EAX: the accept path is
// `mov eax, [edx+10h] / mov eax, [eax+1Ch] / ret` and the four reject paths
// share `xor eax, eax / ret`, so under `void` MSVC deletes the load and no
// shape expresses the body. The pointee is the field's: TAI_Captain::pUnit1C
// is a CUnit *.
CUnit *tai_soldier_captain_air_threat(CUnit *pUnit);
// 0x4AE870 tai_soldier_captain_ground_threat (45 B). One register argument.
// The same body as tai_soldier_captain_air_threat, reading pUnit20 (+0x20),
// which is also a CUnit *.
CUnit *tai_soldier_captain_ground_threat(CUnit *pUnit);
// 0x4AE8A0 captain_ptr_to_id (56 B). One register argument.
int captain_ptr_to_id(TAI_Captain *pCaptain);
// 0x4AE8E0 tai_soldier_captain_is_idle (28 B). One register argument.
int tai_soldier_captain_is_idle(CUnit *pUnit);
// 0x4AE900 captain_id_to_ptr (83 B). One register argument.
TAI_Captain *captain_id_to_ptr(int nCaptainId);
// 0x4AEAA0 tai_captain_unit_region_is_calm (79 B). One register argument.
BOOL tai_captain_unit_region_is_calm(CUnit *pUnit);
// 0x4AF220 tai_call_attackers_on_unit (79 B). One register argument.
void tai_call_attackers_on_unit(CUnit *pUnit);
// 0x4AF270 tai_call_attackers_if_disabled (78 B). One register argument.
void tai_call_attackers_if_disabled(CUnit *pUnit);
// 0x4AF2C0 tai_call_attackers_when_hit (55 B). One register argument.
void tai_call_attackers_when_hit(CUnit *pUnit);
// 0x4AF490 aiscript_enter_bunker (137 B). Two register arguments.
void aiscript_enter_bunker(int nNation, rect32_t *pArea);
// 0x4AF520 aiscript_enter_transport (131 B). Two register arguments.
void aiscript_enter_transport(int nNation, rect32_t *pArea);
// 0x4AF6D0 aiscript_exit_transport (120 B). Two register arguments.
void aiscript_exit_transport(int nNation, rect32_t *pArea);
// 0x4AF920 tai_captain_free (5 B). Nullary.
void tai_captain_free();
// 0x4AFAF0 tai_soldier_captain_leader (45 B). One register argument.
// It returns a `CUnit *` because +0x24 is one (see the field). That makes the
// family uniform: tai_soldier_captain_air_threat returns pUnit1C,
// tai_soldier_captain_ground_threat pUnit20 and this one pUnit24, all three
// instruction-identical apart from the final load offset.
CUnit *tai_soldier_captain_leader(CUnit *pUnit);
// 0x4AFEC0 tai_soldier_sibling_boarding (90 B). One register argument.
BOOL tai_soldier_sibling_boarding(CUnit *pUnit);
// 0x4B0030 tai_captain_claim_defence_region (101 B). Two register arguments
// and one stack word (`ret 4h`).
void tai_captain_claim_defence_region(int nArg0, int nArg1, int nArg2);
// 0x4B0210 tai_cloak_if_allowed (52 B). One register argument.
void tai_cloak_if_allowed(CUnit *pUnit);
// 0x4B0350 tai_captain_find_spare_hydralisk (107 B). One register argument.
// ECX is only forwarded to a callee, so this body shows no width for it.
// The returned EAX is ESI or EDI, both walked as CUnit * (+0x64 wType, +0x7C,
// +0x5C, +0x134, +0x6C pPlayerUnitNext) and both out of
// CUnit::GetPlayerUnitList.
CUnit *tai_captain_find_spare_hydralisk(int nPlayer);
// 0x4B03C0 tai_captain_find_spare_mutalisk (107 B). One register argument. ECX
// is only forwarded to a callee, so this body shows no width for it.
// Not void: this is tai_captain_find_spare_hydralisk's twin (the only
// difference is `cmp word ptr [esi+64h], 2Bh` against 26h) and it carries an
// EDI accumulator zeroed at entry, set to ESI in the bType == 4 arm and
// materialised at both exits, which `void` would drop.
CUnit *tai_captain_find_spare_mutalisk(int nPlayer);
// 0x4B05C0 aiscript_wait_bunkers (55 B). One register argument.
BOOL aiscript_wait_bunkers(int nArg0);
// 0x4B0600 aiscript_wait_turrets (55 B). One register argument.
BOOL aiscript_wait_turrets(int nArg0);
// 0x4B0960 tai_captain_for_unit (27 B). One register argument.
// The whole body is `&sgCaptain[pUnit->bPlayer][GetRegion_CUnit(pUnit)]`:
// `movzx eax, ax / imul eax, eax, 34h / add eax, [ecx*4+6B1A74h]` and nothing
// else.
TAI_Captain *tai_captain_for_unit(CUnit *pUnit);
// 0x4B0980 tai_captain_enlist_as_attacker (64 B). Two register arguments.
// arg1 is a CUnit *: `mov edi, edx / mov ecx, edi / call GetRegion_CUnit`, and
// CUnitSearches.h types that parameter `CUnit *` too.
void tai_captain_enlist_as_attacker(CUnit *pUnit, CUnit *pUnit2);
// 0x4B0CE0 tai_captain_service (92 B). Two register arguments.
// `__declspec(noinline)` steers the caller, not this body: without it /Ob2
// splices these 92 bytes into tai_captain_service_round_robin's loop, which
// then runs 284 B against the oracle's 160 with the jump table inline. The
// keyword has to live on the declaration, because the generated TUs see only
// this header.
__declspec(noinline) void tai_captain_service(int nNation, int nArea);
// 0x4B0D70 tai_captain_service_round_robin (160 B). Nullary.
void tai_captain_service_round_robin();
// 0x4B0EC0 aiscript_assign_all_order (75 B). Two register arguments and two
// stack words (`ret 8h`).
// The stack parameter is one by-value rect16_t, not two words: `ret 8h` is
// sizeof(rect16_t), and the only use of the slot is `lea ecx, [ebp+8h] / call
// get_units_in_rect`, which order.h types `CUnit **__fastcall
// get_units_in_rect(rect16_t *)`.
void aiscript_assign_all_order(int nNation, int nOrderId, mrect_t rect);  // Hyperion: map positions
// 0x4B0FE0 aiscript_sharedvision (43 B)
//   `ret 4h`; EDX read before written -> a second register argument
//     ECX is read as 8 bits
//     EDX is read as 8 bits
//   `ret 4h` -> 1 stack word(s)
//     [ebp+08h] is read 4-byte x1
//   returns: EAX at the ret comes from w32 and no caller consumes it -> void
//   NOTE: EAX IS defined at the ret, but by w32 rather than an explicit
//         value, and no caller reads it -- an incidental leftover, which is
//         why `void` is emitted. If a caller elsewhere consumes the result
//         this is an `int`, and the byte gate cannot tell the two apart.
void aiscript_sharedvision(int nArg0, BYTE bArg1, int nArg2);
// 0x4B1380 tai_resolve_carrier_target (72 B). Two register arguments.
// Both parameters and the return are CUnit *: `cmp word ptr [esi+64h], 49h`
// (wType), `mov eax, [esi+0C0h]`, `test dword ptr [edi+0DCh], 20000h`
// (dwFlags), and the three exits return esi, [esi+0C0h] and 0.
CUnit *tai_resolve_carrier_target(CUnit *pUnit0, CUnit *pUnit1);
// 0x4B13D0 tai_captain_assign_unit_to_region (71 B). Two register arguments
// and one stack word (`ret 4h`).
void tai_captain_assign_unit_to_region(CUnit *pUnit, int nArg1, int nArg2);
// 0x4B1420 tai_captain_request_turret_build (79 B). Two register arguments.
void tai_captain_request_turret_build(intptr_t nArg0, int nArg1);
// 0x4B1470 tai_captain_take_turret_site (85 B). Two register arguments.
// arg1 is a point16_t * out-param -- tai_captain_take_bunker_site's twin, same
// `mov edx, ebx / call GetRegionPosition`.
BOOL tai_captain_take_turret_site(int nNation, mpoint_t *pPos);
// 0x4B14D0 tai_unit_is_forming_or_incomplete (34 B). One register argument.
BOOL tai_unit_is_forming_or_incomplete(CUnit *pUnit);

// The TU's larger rows, declared here but not written in this tree: each is
// called by a body above, so the generated thunk needs the name declared in a
// header.

// 0x4AA8F0 (177 B). One register argument, no stack parameters.
// `noinline` is for tai_captain_service_round_robin's sake: at 177 B /Ob2
// splices this whole body into it, and that row runs 359 B against 160.
__declspec(noinline) void tai_captain_start_expansion(int nArg0);

// 0x4AB330 (234 B). `ret 4h` -> two register arguments and one stack word.
// The stack parameter is an int region id, not a pointer: the body compares it
// against the incoming EDX (`mov edi, [ebp+8h]` / `cmp eax, edi`), and
// tai_captain_regions_connected_xy produces it with
// `call GetRegion_xy / movzx eax, ax / push eax`.
BOOL tai_captain_regions_connected(int nArg0, int nArg1, int nArg2);

// 0x4AB900 sai_soldier_set_type (319 B).
// arg0 is a TAI_Captain *: tai_captain_job_defend reaches it as `mov edx, 6h /
// mov ecx, esi / call sai_soldier_set_type`, with ESI holding the captain it
// has been dereferencing at +0x1C, +0x20 and +0x08.
void sai_soldier_set_type(TAI_Captain *pCaptain, int nType);

// 0x4ABF90 (239 B).
// `noinline` is for tai_captain_service_round_robin's sake, like
// tai_captain_start_expansion above: at 239 B /Ob2 splices this in and that row
// runs 440 B against 160.
__declspec(noinline) void tai_captain_recompute_threats(int nArg0);

// The six jump-table arms of tai_captain_service, one per TAI_Captain::bJob
// value. Each is reached as `mov ecx, eax / jmp <arm>` where EAX is
// `&sgCaptain[nation][area]`, so ECX is a captain.
void tai_captain_job_under_attack(TAI_Captain *pCaptain);
void tai_captain_job_attack(TAI_Captain *pCaptain);
void tai_captain_job_gather(TAI_Captain *pCaptain);
void tai_captain_job_lost(TAI_Captain *pCaptain);
void tai_captain_job_reinforce(TAI_Captain *pCaptain);

// 0x4ADBD0 (275 B) / 0x4ADCF0 (225 B).
// Both are called as `mov esi, ecx / call <fn>` from tai_captain_job_defend
// and tai_captain_job_idle, whose ECX is the captain, forwarded.
void tai_captain_recount_strength(TAI_Captain *pCaptain);
void tai_captain_retarget_soldiers(TAI_Captain *pCaptain);

// 0x4AE760 (224 B). Two register arguments and one stack word.
// Both register arguments are pointers: tai_captain_assign_unit_to_region
// computes ECX as `imul ecx, eax, 34h / add ecx, [eax*4+6B1A74h]`, the
// &sgCaptain[n][a] shape, and EDX as the CUnit it is walking;
// tai_captain_enlist_guard_cb and tai_captain_enlist_as_attacker's tail do the
// same. `noinline` is for tai_captain_release_all_soldiers's sake, the lever
// tai_captain_unlink_soldier also carries: at 224 B /Ob2 splices it in.
__declspec(noinline) void tai_soldier_add_to_captain(TAI_Captain *pCaptain, CUnit *pUnit, int nFlag);

// patrol_pick_area -- 0x4AFB20, 529 B. Declared here because
// CUnit::AIUpdateOverlord (SAI_Build.cpp) and CUnit::DispatchAIPatrol call it
// and no header of ours did. `ret 4h`, one stack word; ECX is a pointer and EDX
// a second register argument read 32 bits wide, and both call sites discard
// EAX, so void. The two trailing arguments are map-position out-params:
// AIUpdateOverlord reserves two DWORD locals, passes their addresses, then
// pushes both values into CUnit::SetDestination_xy. The pointee is `int`, not
// WORD -- the caller reads both slots 32 bits wide and the body forwards the
// same pointers into GetRegionCenter, which src117/h/sai.h declares
// `void GetRegionCenter(WORD, int *, int *)`.
void patrol_pick_area(CUnit *pUnit, int *pOutX, int *pOutY);

// tai_captain_init -- 0x4B0640, 299 B. Called by sai_init and sai_main_init.
void tai_captain_init(int nArg0);

// TAI_Captain_RemoveUnit -- 0x4B0770, 438 B. Called by SAI_RemoveUnit and
// aiscript_switch_rescue. The `CUnit *` is the call sites' supply for
// contracts' `void *`, which could only say "a pointer"; both sites in
// SAI_Main.cpp pass a unit. 4 bytes either way, so it cannot move a byte.
BOOL TAI_Captain_RemoveUnit(CUnit *pUnit, int nFlag);
// Hyperion: the captains' tables written as a whole, so the next removal walks every
// area again and makes its sets of the areas whose slots may hold a unit. A
// snapshot's restore calls it; the tables' own makers, readers and growers do.
void CaptainSlotsForget();

// tai_review_allied_strength -- 0x4ACDF0, 1074 B. Called by ai_periodic_timer_tick. Nullary, void.
void tai_review_allied_strength();

// ===========================================================================
// Every name below is defined in TAI_Captain.cpp and no header of ours declared
// it, so gen.py::find_decl would have resolved the callee out of the
// quarantined tree. Contracts are contracts.py's; the pointee types are read
// off the offsets each body dereferences.
// ===========================================================================

// 0x4AA500 (352 B). ECX is a captain by the +0x04 nation read; EDX is a DWORD
// out-buffer tai_captain_request_missing_units gives 0x104 bytes and indexes back with
// `mov edi, [ebp+esi*4-104h]`; the return is the fill count.
int tai_captain_list_missing_harass_types(TAI_Captain *pCaptain, int *pnOut, int bSkipBusy);

// 0x4AE960 (261 B). ECX is a captain by the +0x00 WORD, +0x02 and +0x04 reads;
// EDX is a unit type, the buffer element tai_captain_request_missing_units bounds-checks against 0xE4
// and the body forwards to tai_captain_find_spare_soldier / tai_captain_find_loose_soldier as their type argument.
BOOL tai_captain_enlist_type(TAI_Captain *pCaptain, int nType, int nLimit);

// 0x4AB1D0 (277 B). ECX is a nation: tai_captain_start_expansion passes
// the same value it then uses as the first sgCaptain index.
BOOL tai_captain_harass_list_filled(int nNation);

// 0x4AB490 (313 B). Not authored this wave. Both register arguments are the
// same captain at both call sites (`mov edx, edi / mov ecx, edi` in tai_captain_job_attack
// and tai_captain_job_gather); ECX is dereferenced at +0x00, +0x04 and +0x30.
int tai_captain_cannot_fight_region(TAI_Captain *pCaptain, TAI_Captain *pCaptain2);

// 0x4ABA70 (538 B). Not authored this wave. Returns a CUnit *: tai_captain_job_attack and
// tai_captain_job_gather both feed the result straight into GetRegion_CUnit.
CUnit *tai_captain_pick_attack_target(TAI_Captain *pCaptain);

// 0x4ABE90 (254 B). Not authored this wave. ECX is a nation by
// `mov [ecx*4+6B68C0h], eax` and `imul eax, ecx, 9C4h`.
void tai_captain_flood_reachable_regions(int nNation, int nArg1);

// 0x4A9B20 (790 B). Not authored this wave. `ret 8h`. ECX is a CUnit by the
// +0x134 read, which the body then checks at +0x08 and +0x0C exactly as
// tai_captain_retarget_soldiers does.
void tai_soldier_move_toward(CUnit *pUnit, int nArg1, int nArg2, int nArg3);

// 0x4B15D0 (306 B). Not authored this wave. `ret 0Ch`. Both register arguments
// are captains, each dereferenced at +0x28.
void tai_captain_transfer_soldiers(TAI_Captain *pCaptain, TAI_Captain *pCaptain2,
                int nArg2, int nArg3, int nArg4);

// 0x4AFD40 (376 B). Not authored this wave. ECX is a CUnit by the +0x134 and
// +0x4C reads.
int AIMoveWhereNeeded(CUnit *pUnit);

// ---------------------------------------------------------------------------
// The bodies themselves.
// ---------------------------------------------------------------------------

// 0x4AA9B0 (240 B). ECX is a captain by the +0x0C..+0x12 WORD compares.
BOOL tai_captain_disband_if_weak(TAI_Captain *pCaptain);

// 0x4AAF70 (200 B). `ret 4h`. ECX is a CUnit by the +0x64 and +0x134 reads;
// EDX is a unit type, compared against that +0x64 WORD.
int tai_captain_unit_is_recruitable(CUnit *pUnit, int nUnitType, int nArg2);

// 0x4AB040 (161 B). ECX is a captain by the +0x04 nation read.
void tai_captain_request_missing_units(TAI_Captain *pCaptain);

// 0x4AB6D0 (132 B) / 0x4AB760 (165 B). ECX is a player (a shift count in the
// first, CUnit::GetPlayerUnitList's argument in the second); EDX is the
// 0x9C4-entry region state array both write through.
void tai_captain_mark_enemy_regions(int nPlayer, BYTE *pStates);
void tai_captain_mark_own_regions(int nPlayer, BYTE *pStates);

// 0x4ADE10 (192 B) / 0x4ADED0 (175 B). ECX is a nation, the first sgCaptain
// index in both.
void TAI_Captain_aiscript_build_bunkers(int nNation);
void TAI_Captain_aiscript_build_turrets(int nNation);

// 0x4AEAF0 (198 B). `ret 4h`. ECX is a player (the first sgCaptain index and
// the free IsEnemyUnit's first argument); EDX is a region. The stack word is
// tested for zero on entry and then reused as GetRegionCenter's Y out-slot.
// tai_captain_region_enemy_strength is not declared here. src117/h/sai_TownBuilder.h:881 already
// seats it with the same contract, and a second header declaration makes
// gen.py::find_decl report the callee as ambiguous and emit nothing.

// 0x4AF750 (186 B). `ret 8h`. Returns the nearest matching unit, so a CUnit *;
// ECX is the player it hands CUnit::GetPlayerUnitList and EDX a unit type.
CUnit *tai_captain_find_spare_unit_near(int nPlayer, int nUnitType, int nX, int nY);

// 0x4B0AF0 (198 B) / 0x4B0E10 (169 B) / 0x4B1010 (237 B) / 0x4B1100 (171 B).
// Each takes one CUnit in ECX, dereferenced at +0x4C, +0x134 or +0x64 within
// the first few instructions.
BOOL patroller_follow_captain(CUnit *pUnit);
void tai_soldier_step_to_captain_region(CUnit *pUnit);
void tai_soldier_update(CUnit *pUnit);
// tai_soldier_try_stim is not declared here. src117/h/CUnitCombat.h:155 already
// seats it, and two header declarations of one name make gen.py::find_decl
// report the callee as ambiguous and emit nothing. The seat belongs in this
// header on ownership grounds and moving it is its own change.

// 0x4B1500 (201 B). ECX is a captain: the body walks +0x30 and returns its
// soldiers to the +0x2C pool.
void tai_captain_release_all_soldiers(TAI_Captain *pCaptain);

// 0x4B0F10 (206 B). ECX is a nation; EDX is the aiscript area rectangle, the
// same rect32_t every other aiscript handler in this TU takes.
void aiscript_make_patrol(int nNation, rect32_t *pArea);

// ---------------------------------------------------------------------------
// The rest of the TU's rows. Contracts are contracts.py's; pointee types are
// read off the offsets each body dereferences.
// ---------------------------------------------------------------------------

// 0x4B0BE0 (247 B). `ret 4h`. ECX is the nation, EDX and the stack word a map
// position; SAI_Build.cpp and SAI_Scripts.cpp both call it.
void tai_captain_defend_paths_to_enemies(int nNation, int nX, int nY);

// 0x4B0250 (249 B) / 0x4B0430 (322 B). `ret 4h`. ECX is a player; EDX and the
// stack word are CUnit out-slots the caller reads back as pointers.
BOOL tai_captain_find_two_dark_templar(int nPlayer, CUnit **ppFirst, CUnit **ppSecond);
BOOL tai_captain_find_two_high_templar(int nPlayer, CUnit **ppFirst, CUnit **ppSecond);

// 0x4B09C0 (293 B) / 0x4AFF20 (260 B). ECX is a CUnit by the +0x134 soldier read.
BOOL tai_soldier_is_in_position(CUnit *pUnit);
BOOL tai_soldier_follower_lagging(CUnit *pUnit);

// The aiscript captain handlers. ECX is the nation in each.
void TAI_Captain_aiscript_implode(int nNation);
void TAI_Captain_aiscript_capt_expand(int nNation, int nPass);
void TAI_Captain_aiscript_guard(int nNation, CUnit *pUnit);
BOOL TAI_Captain_aiscript_attack(int nNation, int nArg1, int nArg2, int nArg3, int nArg4);
void aiscript_help_iftrouble(int nNation);

// 0x4AF5B0 (281 B). `ret 4h`. ECX is the nation; the two remaining words are
// forwarded to TAI_Captain_aiscript_attack.
BOOL aiscript_eval_harass(int nNation, int nArg1, int nArg2);

// 0x4AC530 (341 B). EDX is read by nothing: the body never touches it.
int tai_captain_enlist_need_list(TAI_Captain *pCaptain, TAI_Captain *pTarget, const WORD *pNeed, BOOL bAir);

// 0x4AC3C0 (363 B). `ret 8h`. The stack words are an air/ground switch and a cap.
int tai_captain_raise_defence(TAI_Captain *pCaptain, TAI_Captain *pTarget, BOOL bAir, unsigned nMax);

// 0x4AC230 (395 B) / 0x4AC0D0 (342 B). Both return the soldier they enlisted.
TSoldier *tai_captain_find_spare_soldier(TAI_Captain *pCaptain, int nType, int nX, int nY, TAI_Captain *pOther);
TSoldier *tai_captain_find_loose_soldier(TAI_Captain *pCaptain, int nLimit, int nType, int nX, int nY,
                     TAI_Captain *pOther);

// 0x4AE220 (341 B). EDX is the nation; ECX is a flag the body only tests.
void tai_captain_crew_bunkers(intptr_t nArg0, int nNation);

// 0x4AA350 (362 B). `ret 4h`. EDX is the count of the DWORD region list on the
// stack; the return is an index into it.
int tai_captain_pick_weakest_near_region(int nNation, unsigned nCount, const int *pnRegions);

// 0x4ABD00 (385 B). ECX is a CUnit by the +0xDC flags read.
void tai_send_unit_at_nearest_enemy(CUnit *pUnit);

// 0x4B1710 (255 B) / 0x4B1810 (541 B). `ret 14h` each. EDX is the 0x9C4-entry
// region state array tai_captain_choose_attack_target forwards unchanged; tai_captain_find_staging_region returns an int
// score in EAX.
BOOL tai_captain_choose_attack_target(int nNation, const BYTE *pbStates, int nArg0, int nArg1, int nArg2,
                int nArg3, int nArg4);
int tai_captain_find_staging_region(int nNation, int nRegion, const BYTE *pbStates, int nArg0, int nArg1,
               int nArg2, int nArg3);

// 0x4AE080 (402 B). `ret 4h`. Returns whichever of EDX and the stack word ECX
// should attack.
CUnit *tai_pick_better_target(CUnit *pUnit, CUnit *pFirst, CUnit *pSecond);

// 0x4AAC20 (412 B). `ret 4h`. EDX and the stack word are map-position out-params.
BOOL tai_pick_spider_mine_spot(CUnit *pUnit, int *pnX, int *pnY);

// 0x4AD550 (1041 B). `ret 8h`. EDX selects one of fourteen rush tests; the stack
// words are the target position.
BOOL TAI_Captain_aiscript_should_rush(int nNation, int nCase, int nX, int nY);

// 0x4B11B0 (455 B). ECX is the nation; both callers set only DL, so the flag is a byte.
void tai_send_everything_to_attack(int nNation, BYTE bAll);

// 0x4AA660 (574 B). `ret 4h`. ECX is the attacked unit, EDX the attacker; the
// stack word limits the answer to captains' soldiers.
void tai_call_allies_on_attacker(CUnit *pUnit, CUnit *pAttacker, int bCaptainsOnly);

// 0x4AADF0 (188 B). ECX is the unit, EDX the target handed to ai_get_flee_target;
// tai_unit_was_attacked tests the result twice and discards it twice.
BOOL tai_reposition_for_attack(CUnit *pUnit, void *pArg1);

// The captain save codec, called from SAI_Main.cpp's read_save_ai_data and
// write_save_ai_data.
int sai_read_captain_data(intptr_t nFile);
int sai_write_captain_data(intptr_t nFile);
