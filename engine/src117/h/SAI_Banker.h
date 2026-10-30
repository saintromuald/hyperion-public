// SAI_Banker.h -- the per-player AI banker record.
//
// The array is `extern AI_Banker gAIBanker[8]` at VA 0x67D068. Its stride is
// 0x4E8, from every accessor's `imul e?x, ecx, 4E8h`, and the count 8, from
// `cmp ecx, 8h` in set_harass and ai_plan_next_nation and from the 0x2740 bytes
// SAI_Banker_LoadFromSave zeroes from the base (0x2740 == 8 * 0x4E8). The record
// is packed edge to edge: across every oracle function using that stride the
// lowest displacement is 0x67D068 and the highest field ends at 0x67D550.
//
// The table is expected to grow. A field lands here only with an instruction
// citing both its offset and its width, because a WORD field declared DWORD
// emits `mov` where the oracle emits `movzx`.
#ifndef OC117_SAI_BANKER_H
#define OC117_SAI_BANKER_H

#include "platform.h"  // OC_X86_LAYOUT
#include <stddef.h>     // offsetof, for the layout static_asserts below
#include "pos.h"        // Hyperion: spos_t (stage E)

#ifndef OC117_BASIC_TYPES
typedef unsigned char  BYTE;
typedef unsigned short WORD;
#include "platform.h"  // DWORD
#include "typelimits.h"  // Hyperion: tech_id_t, upgrade_id_t
#endif

// `class`, not `struct` -- src117/h/CUnit.h defines it as a class, and a tag
// mismatch raises C4099 in any TU that includes both.
class CUnit;    // incomplete by design -- only a pointer to it is stored below

// A 16-byte blob copied whole (`movdqu`, so alignment is under 16). The size and
// the alignment are the only proven facts; the member layout is a placeholder,
// since any 16-byte POD of alignment 4 or less emits identical code.
struct AI_GenCmd { /*0x000*/ DWORD dw[4]; };

// One record of the banker's demand heap. dwCaptain is whatever the demand was
// made for (a captain, guard, town or request record), 0 for none; the callers
// cast it back to the pointer they expect, so it is pointer-sized.
struct SAI_Demand {
  /*0x00*/ BYTE bPrio;           // sai_pop_demand compares it with `mov cl, [..+18h]`
  /*0x01*/ BYTE bKind;           // sai_add_demand `mov byte ptr [ebp-7h], bl`
  /*0x02*/ WORD wArg;            // sai_add_demand `mov word ptr [ebp-6h], di`
  /*0x04*/ DWORD_PTR dwCaptain;  // sai_add_demand `mov [ecx+edi*8+1Ch], esi`
};
static_assert(offsetof(SAI_Demand, bPrio) == 0x00, "SAI_Demand.bPrio");
static_assert(offsetof(SAI_Demand, bKind) == 0x01, "SAI_Demand.bKind");
static_assert(offsetof(SAI_Demand, wArg) == 0x02, "SAI_Demand.wArg");
OC_X86_LAYOUT(offsetof(SAI_Demand, dwCaptain) == 0x04, "SAI_Demand.dwCaptain");
OC_X86_LAYOUT(sizeof(SAI_Demand) == 8, "SAI_Demand stride (eax+edx*8)");

// The /*0xNN*/ comment leads each declaration rather than trailing it: the
// layout checker reads only a leading one, and a field it cannot see is left
// unchecked against the asserts below.
struct AI_Banker {
    // Six DWORDs, 0x000..0x018: every access is a `dword ptr` add or cmp, never
    // a movzx. 0x000/0x004/0x008 are accumulated into (banker_demand_nuke and
    // banker_charge_plan_cost `add dword ptr [edx+67D068h], eax`) and
    // 0x00C/0x010/0x014 only compared against (all three ai_stage_*_cost). The
    // ore/gas/supply names are a reading of GetUnitCost/GetTechCost/
    // GetUpgradeCost's out-params, which land in the first group in that order.
    /*0x000*/ DWORD dwCostOre;          // abs 0x67D068
    /*0x004*/ DWORD dwCostGas;          // abs 0x67D06C
    /*0x008*/ DWORD dwCostSupply;       // abs 0x67D070; summed from a BYTE out-param
    /*0x00C*/ DWORD dwAvailOre;         // abs 0x67D074; unsigned cmp (jnb/jb)
    /*0x010*/ DWORD dwAvailGas;         // abs 0x67D078; unsigned cmp
    /*0x014*/ DWORD dwAvailSupply;      // abs 0x67D07C; unsigned cmp
    // abs 0x67D080. The demand max-heap, 63 records edge to edge up to the count
    // at 0x210: 0x210 - 0x018 == 0x1F8 == 63 * 8, and sai_add_demand caps the
    // count at 0x3F. The three sai_top_demand_* readers each read the top one.
    /*0x018*/ SAI_Demand aDemands[63];
    // abs 0x67D278. The demand-heap count: sai_top_demand_type guards on
    // `cmp byte ptr [eax+210h], 0h` and sai_pop_demand pops with
    // `mov cl,[eax+210h] / sub cl,1 / mov [eax+210h],cl`.
    /*0x210*/ BYTE bDemandCount;
    /*0x211*/ BYTE bInUse;              // abs 0x67D279; banker_set_build_site_failed stores 1, banker_build_site_failed tests it
    /*0x212*/ BYTE bNukeRate;           // abs 0x67D27A
    /*0x213*/ BYTE bAttacks;            // abs 0x67D27B
    /*0x214*/ DWORD dwLastNukeTime;     // abs 0x67D27C; aiscript_fake_nuke, banker_set_last_nuke_time
    /*0x218*/ WORD wFlags;              // abs 0x67D280; bit set/test/clear across 34 corpus sites
    /*0x21A*/ WORD wPanicBlock;         // abs 0x67D282
    /*0x21C*/ WORD wMaxForce;           // abs 0x67D284
    /*0x21E*/ WORD wUnk21E;             // abs 0x67D286; banker_attack_target_area reads, aiscript_attack_clear writes
    /*0x220*/ WORD wPendingPlus1;       // abs 0x67D288; stored +1, 0 == unset (sentinel bias)
    /*0x222*/ BYTE bDefaultMin;         // abs 0x67D28A
    /*0x223*/ BYTE bUnk223;             // abs 0x67D28B; banker_request_budget `movzx eax, byte ptr`
    // abs 0x67D28C. A timestamp written from the elapsed-time global by
    // banker_stamp_request and read back by banker_request_throttle_passed
    // against a 30-unit threshold. The name is ours.
    /*0x224*/ DWORD dwStamp224;
    // abs 0x67D290. Unsigned: its consumer banker_attack_window_expired does
    // cmp/sbb/inc, and an sbb after a cmp reads CF. Written by
    // aiscript_attack_clear, banker_set_attack_target and aiscript_quick_attack
    // from the elapsed-time global.
    /*0x228*/ DWORD dwAttackTime;
    /*0x22C*/ BYTE bUnk22C;             // abs 0x67D294; banker_attack_cooldown reads, banker_set_attack_cooldown writes 0x18
    /*0x22D*/ BYTE bUnk22D;             // abs 0x67D295; banker_ai_spells_allowed `cmp byte ptr [ecx+67D295h], al`
    /*0x22E*/ BYTE bUnk22E;             // abs 0x67D296; banker_hold_attack stores 1
    /*0x22F*/ BYTE bUnk22F;             // abs 0x67D297; small enum, init 1, tested == 2
    // abs 0x67D298, 0x80 bytes == 64 WORDs: aiscript_attack_clear memsets 0x80
    // from it, set_harass stores `mov word ptr [esi], cx` and
    // banker_clear_harass_type strides `add edx, 2h`, and 0x230 + 0x80 == 0x2B0,
    // where the defensebuild group starts. 1.17 bounds the index at 63 in both
    // writers while the memset clears 64, so the last slot is unreachable by
    // them; reproduced, not corrected.
    /*0x230*/ WORD aHarassNeed[64];
    // Eight contiguous WORD[20] arrays, 0x28 apart over 0x2B0..0x3F0, each base
    // read off its own `add e?x, 67Dxxxh` rather than assumed from the pattern:
    // `cmp eax, 14h` bounds the index at 20 and `mov word ptr [esi+eax*2], cx`
    // states the element width. The order is gg/ag/ga/aa in both groups.
    /*0x2B0*/ WORD aDefenseBuildGG[20];  // abs 0x67D318
    /*0x2D8*/ WORD aDefenseBuildAG[20];  // abs 0x67D340
    /*0x300*/ WORD aDefenseBuildGA[20];  // abs 0x67D368
    /*0x328*/ WORD aDefenseBuildAA[20];  // abs 0x67D390
    /*0x350*/ WORD aDefenseUseGG[20];    // abs 0x67D3B8
    /*0x378*/ WORD aDefenseUseAG[20];    // abs 0x67D3E0
    /*0x3A0*/ WORD aDefenseUseGA[20];    // abs 0x67D408
    /*0x3C8*/ WORD aDefenseUseAA[20];    // abs 0x67D430
    // aiscript_define_max: `mov byte ptr [ecx+eax*1+67D458h], dl` gives the
    // element width and the eax*1 scale the element size. The length 228 is an
    // inference, not proven: it is bounded above by the next field
    // (0x4D4 - 0x3F0 == 0xE4 == units.dat's unit count) and banker_at_define_max
    // indexes it by unit type, but no instruction states it, so a shorter array
    // plus tail padding is not excluded.
    /*0x3F0*/ BYTE aDefineMax[228];      // abs 0x67D458
    // abs 0x67D53C. A CUnit *: banker_register_medic does
    // `cmp dword ptr [eax+67D53Ch], 0h` and then stores an ECX the same body
    // dereferences at +0x4C (bPlayer) and +0x64 (wType). It registers the
    // player's first wType == 0x22 unit here, and SAI_Banker_RemoveUnit clears
    // the slot and rescans for another; reading 0x22 as the medic is ours. CUnit
    // stays incomplete, so this header needs no dependency on CUnit.h.
    /*0x4D4*/ CUnit *pMedic;
    /*0x4D8*/ AI_GenCmd genCmd;          // abs 0x67D540; ends exactly at sizeof
};

// The size is asserted as well as the offsets: without it trailing padding goes
// unchecked, and the 0x4E8 stride is the one number the whole family rests on.
static_assert(offsetof(AI_Banker, dwCostOre)       == 0x000, "AI_Banker.dwCostOre");
static_assert(offsetof(AI_Banker, dwCostGas)       == 0x004, "AI_Banker.dwCostGas");
static_assert(offsetof(AI_Banker, dwCostSupply)    == 0x008, "AI_Banker.dwCostSupply");
static_assert(offsetof(AI_Banker, dwAvailOre)      == 0x00C, "AI_Banker.dwAvailOre");
static_assert(offsetof(AI_Banker, dwAvailGas)      == 0x010, "AI_Banker.dwAvailGas");
static_assert(offsetof(AI_Banker, dwAvailSupply)   == 0x014, "AI_Banker.dwAvailSupply");
static_assert(offsetof(AI_Banker, aDemands)        == 0x018, "AI_Banker.aDemands");
OC_X86_LAYOUT(offsetof(AI_Banker, bDemandCount)   == 0x210, "AI_Banker.bDemandCount");
OC_X86_LAYOUT(offsetof(AI_Banker, bInUse)          == 0x211, "AI_Banker.bInUse");
OC_X86_LAYOUT(offsetof(AI_Banker, bNukeRate)       == 0x212, "AI_Banker.bNukeRate");
OC_X86_LAYOUT(offsetof(AI_Banker, bAttacks)        == 0x213, "AI_Banker.bAttacks");
OC_X86_LAYOUT(offsetof(AI_Banker, dwLastNukeTime)  == 0x214, "AI_Banker.dwLastNukeTime");
OC_X86_LAYOUT(offsetof(AI_Banker, wFlags)          == 0x218, "AI_Banker.wFlags");
OC_X86_LAYOUT(offsetof(AI_Banker, wPanicBlock)     == 0x21A, "AI_Banker.wPanicBlock");
OC_X86_LAYOUT(offsetof(AI_Banker, wMaxForce)       == 0x21C, "AI_Banker.wMaxForce");
OC_X86_LAYOUT(offsetof(AI_Banker, wUnk21E)         == 0x21E, "AI_Banker.wUnk21E");
OC_X86_LAYOUT(offsetof(AI_Banker, wPendingPlus1)   == 0x220, "AI_Banker.wPendingPlus1");
OC_X86_LAYOUT(offsetof(AI_Banker, bDefaultMin)     == 0x222, "AI_Banker.bDefaultMin");
OC_X86_LAYOUT(offsetof(AI_Banker, bUnk223)         == 0x223, "AI_Banker.bUnk223");
OC_X86_LAYOUT(offsetof(AI_Banker, dwStamp224)     == 0x224, "AI_Banker.dwStamp224");
OC_X86_LAYOUT(offsetof(AI_Banker, dwAttackTime)    == 0x228, "AI_Banker.dwAttackTime");
OC_X86_LAYOUT(offsetof(AI_Banker, bUnk22C)         == 0x22C, "AI_Banker.bUnk22C");
OC_X86_LAYOUT(offsetof(AI_Banker, bUnk22D)        == 0x22D, "AI_Banker.bUnk22D");
OC_X86_LAYOUT(offsetof(AI_Banker, bUnk22E)         == 0x22E, "AI_Banker.bUnk22E");
OC_X86_LAYOUT(offsetof(AI_Banker, bUnk22F)         == 0x22F, "AI_Banker.bUnk22F");
OC_X86_LAYOUT(offsetof(AI_Banker, aHarassNeed)     == 0x230, "AI_Banker.aHarassNeed");
OC_X86_LAYOUT(offsetof(AI_Banker, aDefenseBuildGG) == 0x2B0, "AI_Banker.aDefenseBuildGG");
OC_X86_LAYOUT(offsetof(AI_Banker, aDefenseBuildAG) == 0x2D8, "AI_Banker.aDefenseBuildAG");
OC_X86_LAYOUT(offsetof(AI_Banker, aDefenseBuildGA) == 0x300, "AI_Banker.aDefenseBuildGA");
OC_X86_LAYOUT(offsetof(AI_Banker, aDefenseBuildAA) == 0x328, "AI_Banker.aDefenseBuildAA");
OC_X86_LAYOUT(offsetof(AI_Banker, aDefenseUseGG)   == 0x350, "AI_Banker.aDefenseUseGG");
OC_X86_LAYOUT(offsetof(AI_Banker, aDefenseUseAG)   == 0x378, "AI_Banker.aDefenseUseAG");
OC_X86_LAYOUT(offsetof(AI_Banker, aDefenseUseGA)   == 0x3A0, "AI_Banker.aDefenseUseGA");
OC_X86_LAYOUT(offsetof(AI_Banker, aDefenseUseAA)   == 0x3C8, "AI_Banker.aDefenseUseAA");
OC_X86_LAYOUT(offsetof(AI_Banker, aDefineMax)      == 0x3F0, "AI_Banker.aDefineMax");
OC_X86_LAYOUT(offsetof(AI_Banker, pMedic)        == 0x4D4, "AI_Banker.pMedic");
OC_X86_LAYOUT(offsetof(AI_Banker, genCmd)        == 0x4D8, "AI_Banker.genCmd");
static_assert(sizeof(AI_GenCmd) == 16, "AI_GenCmd is a 16-byte movdqu blob");
OC_X86_LAYOUT(sizeof(AI_Banker) == 0x4E8, "AI_Banker stride (imul ecx, 4E8h)");

// The unported callees of the byte-matched bodies in src117/lang/SAI_Banker.cpp.
// They are declared in a header, not in the .cpp, because the generator emits a
// thunk only for a callee a header on the include path declares, and src117/h
// comes first on that path, so a declaration here wins. Every signature is read
// off the call sites, and the evidence for each is on the declaration.

// Two records these signatures name; neither is an AI_Banker field.

// TAI_Captain, the per-region AI captain record, lives in its own TU's header.
// Its size is 0x34 (`imul e?x, <region>, 34h` at 60+ oracle sites), which is the
// only property banker_captain_for_request depends on: that body only computes
// &captains[region].
#include "TAI_Captain.h"

// SAI_Request -- the per-player AI request node banker_captain_for_request's
// second argument points at; the type name is ours. It uses sX at +0x12 and sY
// at +0x14, both read `movsx`, so signed 16-bit into a 32-bit parameter. 0x20 is
// a floor, from the highest field the corpus cites (a DWORD at +0x1C), not a
// size: the nodes are heap-allocated list elements with no stride to read.
// Hyperion: the nodes are TGuard records (TAI_GuardPost.h, whose one caller
// casts one), so the view begins with TGuard's two list links; on x64 that keeps
// every field on TGuard's, as TAI_GuardPost.cpp asserts. 12 opaque bytes in 1.17.
struct SAI_Request {
    /*0x00*/ void  *_links[2];
    /*0x08*/ BYTE   _unmapped_08[0x04];
    /*0x0C*/ void  *pParent;
    /*0x10*/ WORD   wType;
    /*0x12*/ spos_t sX;  // Hyperion: TGuard's position, 4 bytes on x64 (stage E)
    /*0x14*/ spos_t sY;
    /*0x16*/ BYTE   _unmapped_16[0x20 - 0x16];
};
// pParent and wType are not reached by any body of ours; they are here because
// removing them would move sX and sY, which are. The asserts hold them in place.
OC_X86_LAYOUT(offsetof(SAI_Request, pParent) == 0x0C, "SAI_Request.pParent [esi+0Ch]");
OC_X86_LAYOUT(offsetof(SAI_Request, wType) == 0x10, "SAI_Request.wType [edx+10h]");
OC_X86_LAYOUT(offsetof(SAI_Request, sX) == 0x12, "SAI_Request.sX [eax+12h]");
OC_X86_LAYOUT(offsetof(SAI_Request, sY) == 0x14, "SAI_Request.sY [eax+14h]");
OC_X86_LAYOUT(sizeof(SAI_Request) == 0x20, "SAI_Request (FLOOR, not proven)");

// oracle 0x482260 / 0x482310, both __fastcall(nation[, flag]). banker_refund_top_demand
// spends a plan's cost out of the banker; sai_pop_demand pops the demand heap
// (`mov cl,[eax+210h] / sub cl,1 / mov [eax+210h],cl`). Neither returns a value
// any caller consumes -- banker_spend_top_demand and banker_drop_top_demand both tail-jmp into the
// second, which is only legal from a void caller with a matching frame.
void __fastcall banker_refund_top_demand(int nation, int alsoDebit);
void __fastcall sai_pop_demand(int nation);

// oracle 0x4AD230 (102 B, TAI_Captain.cpp). aiscript_attack_clear tail-`jmp`s to
// it after `mov ecx, ebx`: one argument, in ECX, no stack args.
void __fastcall tai_captain_cancel_gather_jobs(int nation);

// oracle 0x481B10 (136 B, sai.cpp). Both call sites load the arguments with
// movsx from 16-bit fields and the callee runs `cdq` on each before an
// arithmetic shift, so the parameters are signed 32-bit. The return is 16-bit:
// its two exits are `mov ax, word ptr [...]` -- the upper half of EAX is never
// written -- and both callers immediately `movzx eax, ax`.
WORD __fastcall GetRegion_xy(int x, int y);

// oracle 0x4087C0 (84 B) and its two siblings, the plan-cost queries.
// `mov si, cx` gives arg1 in ECX, two `mov [reg], eax` dword stores are two
// out-params by pointer and `mov byte ptr [edx], cl` makes the third a BYTE *;
// `ret 8h` retires exactly the two stack slots. GetUpgradeCost takes a leading
// nation argument the other two do not, which three call sites agree on.
void __fastcall GetUpgradeCost(int nation, int id, DWORD *outOre, DWORD *outGas);
void __fastcall GetTechCost(int id, DWORD *outOre, DWORD *outGas);
// GetUnitCost's first parameter is a WORD: the prologue is `mov si, cx`, a
// 16-bit copy. An `int` parameter puts a fully defined value in ECX and MSVC then
// emits the one-byte-shorter `mov esi, ecx` (measured: 83 B against the oracle's
// 84); only a 16-bit parameter forces the 16-bit move. The three later
// `movzx eax, si` are that WORD zero-extended once per index.
void __fastcall GetUnitCost(WORD unitType, DWORD *outOre, DWORD *outGas, BYTE *outSupply);

// The three cost functions and sai_banker_GetPlanCost return void: the oracle
// never writes EAX meaningfully and every caller discards it. GetPlanCost's
// `return <void call>;` keeps the oracle's `call X / pop ebp / ret 10h` tail.

// oracle 0x4086B0. The spelling is verbatim src117/lang/order.cpp's, which its
// byte-matched calls are mangled against, so it is fixed. The callers pass 16
// bits (town_pending_costs moves CUnit::GetBuildUnit's 16-bit return into EDX
// with no widening, and count_race, count_food_available, count_food_max and
// bldg_check_food each widen it themselves) while banker.cpp defines it over an
// int, since its body forwards EDX whole to bldg_check_food; C linkage makes
// both views one symbol.
// Hyperion: the unit type is an int, as banker.cpp's definition reads it,
// which the x64 build needs; a unit type is the same widened.
extern "C" BOOL __fastcall bldg_get_unit_cost(BYTE bPlayer, int nUnitType, BOOL bCheckFood, BOOL bShowError);
// The generator parses this plain form.
BOOL __fastcall bldg_get_unit_cost(BYTE bPlayer, int nUnitType, BOOL bCheckFood, BOOL bShowError);

// oracle 0x408650 (28 B), a callee only, with no body of ours: the resource
// deduct mirror of the get/set pair around it. `movzx ecx, cl` makes the one
// __fastcall parameter a BYTE (an int would have kept the full ECX), and
// CUnit::DispatchZergBldgMorph loads `mov cl, byte ptr [esi+4Ch]`, the player
// byte, immediately before the call.
void __fastcall bldg_deduct_resources(BYTE bPlayer);

// oracle 0x408670 / 0x408710, the tech and upgrade affordability probes. For a
// parameter the callee is the authority, and both read their first two eight
// bits wide with nothing wider anywhere (`movzx edx, cl` / `movzx esi, dl` and
// `mov bh, cl` / `mov bl, dl`), so BYTE, BYTE; CUnit::StartResearch's
// `mov cl, byte ptr [esi+4Ch] / mov dl, bl` agrees, where an `int` parameter
// would have to `movzx`. The third parameter is a dword (`[ebp+8h]` read 4-byte,
// `ret 4h`) and the return an int (both call sites `test eax, eax`).
int __fastcall bldg_get_tech_cost(BYTE bPlayer, tech_id_t bTechId, int nQuiet);
int __fastcall bldg_get_upgrade_cost(BYTE bPlayer, upgrade_id_t bUpgradeId, int nQuiet);

// oracle 0x482040. `mov dl, 8h` at banker_demand_nuke's call site makes the
// second __fastcall parameter a BYTE, not an int, which would have emitted
// `mov edx, 8`; three arguments are pushed on top of the two register ones.
// The fourth parameter is a BYTE as well: AddPrereqUnitToAINeeds (0x482130)
// pushes a register whose upper 24 bits are still the caller's saved value
// (`mov bl, byte ptr [ebp+8h] / dec bl / push ebx`), legal only if the callee
// reads one byte of the slot, and an `int` formal forces `movzx edi, al /
// push edi`, 3 bytes more at each of two sites. The callee's own read of the
// slot is 4 bytes wide with one byte used, so it does not say otherwise.
void __fastcall sai_add_demand(int nation, BYTE kind, int unitType, BYTE a4, intptr_t a5);

// The two unit-count queries. Second parameter is a BYTE (`mov dl, <player>` at
// every call site); the result is accumulated with a plain `add`, and
// aiscript_count_units returns it unwidened, so it is 32-bit. count_get_existence's
// type is a WORD: town_player_tick passes it with `mov cx, word ptr` and the callee widens it.
unsigned __fastcall count_get_existence(WORD unitType, BYTE nation);
unsigned __fastcall count_get_completion(int unitType, BYTE nation);


// The bodies -- src117/lang/SAI_Banker.cpp, in oracle VA order. They are
// declared here because the generator needs a header for every hooked symbol,
// not for any caller of ours: nothing in our tree calls them, the AI script VM
// does, from inside the oracle. Each signature restates the .cpp's definition
// token for token -- the mangled name carries the return and parameter types,
// and the hook table is compiled against this file while the body is compiled
// from that one -- and each VA and size is the oracle's. Read the .cpp for the
// derivation of any one of them; this is an index, not an argument.

void __fastcall sai_banker_GetPlanCost(int nation, int planType, int id, DWORD *outOre, DWORD *outGas, BYTE *outSupply);  // oracle 0x481FF0 76B
BOOL __fastcall banker_is_player_medic(CUnit *unit);  // oracle 0x482450 22B
int __fastcall banker_plan_is_secure(int nation);  // oracle 0x482470 17B
void __fastcall banker_start_nation(int nation);  // oracle 0x482490 26B
int __fastcall banker_has_defenseuse_gg(int nation, int unitType);  // oracle 0x4824B0 47B
WORD * __fastcall get_defensebuild_aa(int nation);  // oracle 0x4824E0 12B
WORD * __fastcall get_defensebuild_ga(int nation);  // oracle 0x4824F0 12B
WORD * __fastcall get_defensebuild_ag(int nation);  // oracle 0x482500 12B
WORD * __fastcall get_defensebuild_gg(int nation);  // oracle 0x482510 12B
void __fastcall banker_register_medic(CUnit *pUnit);  // oracle 0x482520 46B
void __fastcall aiscript_defensebuild_aa(int nation, unsigned count, unsigned unitType);  // oracle 0x482550 88B
void __fastcall aiscript_defensebuild_ga(int nation, unsigned count, unsigned unitType);  // oracle 0x4825B0 88B
void __fastcall aiscript_defensebuild_ag(int nation, unsigned count, unsigned unitType);  // oracle 0x482610 88B
void __fastcall aiscript_defensebuild_gg(int nation, unsigned count, unsigned unitType);  // oracle 0x482670 88B
int __fastcall ai_stage_unit_cost(int nation, int id);  // oracle 0x4826D0 79B
int __fastcall ai_stage_tech_cost(int nation, int id);  // oracle 0x482720 77B
int __fastcall ai_stage_upgrade_cost(int nation, int id);  // oracle 0x482770 77B
int __fastcall banker_request_budget(int nation);  // oracle 0x4827C0 14B
void __fastcall aiscript_defenseclear_aa(int nation);  // oracle 0x4827D0 58B
void __fastcall aiscript_defenseclear_ga(int nation);  // oracle 0x482810 58B
void __fastcall aiscript_defenseclear_ag(int nation);  // oracle 0x482850 58B
void __fastcall aiscript_defenseclear_gg(int nation);  // oracle 0x482890 58B
void __fastcall aiscript_creep(int nation, BYTE bValue);  // oracle 0x4828D0 37B
void __fastcall banker_consume_request_budget(int nation);  // oracle 0x482900 48B
void __fastcall banker_demand_nuke(int nation, int unitType, int a3);  // oracle 0x482930 87B
void __fastcall banker_spend_top_demand(int nation);  // oracle 0x482BC0 21B
int __fastcall banker_take_pending_type(int nation, int *out);  // oracle 0x482BE0 39B
int __fastcall get_panic_block(int nation);  // oracle 0x482C10 14B
int __fastcall banker_is_ahead(int nation);  // oracle 0x482C20 19B
void __fastcall aiscript_define_max(int nation, int v, int unitType);  // oracle 0x482C40 23B
void __fastcall aiscript_set_panic_block(int nation, int v);  // oracle 0x482C60 14B
void __fastcall banker_drop_top_demand(int nation);  // oracle 0x482C70 18B
int __fastcall sai_is_creep(int nation);  // oracle 0x482C90 19B
void __fastcall banker_set_is_ahead(int nation, int on);  // oracle 0x482CB0 36B
WORD * __fastcall get_defenseuse_aa(int nation);  // oracle 0x482CE0 12B
WORD * __fastcall get_defenseuse_ga(int nation);  // oracle 0x482CF0 12B
WORD * __fastcall get_defenseuse_ag(int nation);  // oracle 0x482D00 12B
WORD * __fastcall get_defenseuse_gg(int nation);  // oracle 0x482D10 12B
void __fastcall aiscript_defenseuse_aa(int nation, unsigned count, unsigned unitType);  // oracle 0x482D20 88B
void __fastcall aiscript_defenseuse_ga(int nation, unsigned count, unsigned unitType);  // oracle 0x482D80 88B
void __fastcall aiscript_defenseuse_ag(int nation, unsigned count, unsigned unitType);  // oracle 0x482DE0 88B
void __fastcall aiscript_defenseuse_gg(int nation, unsigned count, unsigned unitType);  // oracle 0x482E40 88B
void __fastcall banker_set_build_site_failed(int nation);  // oracle 0x482EA0 14B
void __fastcall banker_set_pending_type(int nation, int v);  // oracle 0x482EB0 15B
// BOOL, not `bool`: the callee emits identical bytes for either
// (`xor eax,eax / cmp byte ptr [ecx+67D279h], al / setnz al / ret`, where the
// `xor` that feeds the `cmp` also clears the high 24 bits), so the callers
// settle it. Both of them, ai_get_build_location_near and
// ai_get_build_location_at_unit, emit `test eax, eax` (85 C0) where a `bool`
// return gives `test al, al` (84 C0).
BOOL __fastcall banker_build_site_failed(int nation);  // oracle 0x482EC0 18B
int __fastcall banker_is_attacking(int nation);  // oracle 0x482F20 19B
void __fastcall aiscript_fake_nuke(int nation);  // oracle 0x482F80 20B
void __fastcall aiscript_set_farms_timing(int nation, int nValue);  // oracle 0x482FA0 32B
int __fastcall aiscript_get_difficulty(int nation);  // oracle 0x482FC0 14B
int __fastcall banker_get_max_force(int nation);  // oracle 0x482FD0 14B
void __fastcall aiscript_give_money(int nation);  // oracle 0x482FF0 53B
int __fastcall aiscript_has_upgrades(int nation);  // oracle 0x483200 19B
void __fastcall SAI_Banker_RemoveUnit(CUnit *pUnit);  // oracle 0x483220 72B
void __fastcall banker_set_last_nuke_time(int nation);  // oracle 0x483270 18B
void __fastcall banker_set_max_force(int nation, int v);  // oracle 0x483290 14B
int __fastcall banker_nuke_ready(int nation);  // oracle 0x4832A0 39B
void __fastcall banker_set_attacking(int nation);  // oracle 0x4832D0 19B
int __fastcall banker_all_defensebuild_empty(int nation);  // oracle 0x4832F0 55B
int __fastcall banker_farms_timing(int nation);  // oracle 0x483330 20B
int __fastcall banker_transports_wanted(int nation);  // oracle 0x483350 20B
void __fastcall aiscript_nuke_rate(int nation, int v);  // oracle 0x483370 13B
int __fastcall banker_attack_cooldown(int nation);  // oracle 0x483380 14B
int __fastcall banker_may_attack_expansion(int nation);  // oracle 0x483390 38B
BOOL __fastcall banker_ai_spells_allowed(int nation);  // oracle 0x4833C0 18B
BOOL __fastcall banker_attack_not_held(int nation);  // oracle 0x4833E0 18B; its one caller tests all of EAX
void __fastcall banker_set_attack_cooldown(int nation);  // oracle 0x4834B0 14B
void __fastcall aiscript_set_attacks(int nation, int v);  // oracle 0x4834C0 13B
void __fastcall banker_attack_done(int nation);  // oracle 0x4834D0 37B
void __fastcall banker_hold_attack(int nation);  // oracle 0x483500 14B
void __fastcall aiscript_target_expansion(int nation);  // oracle 0x483510 19B
intptr_t __fastcall sai_top_demand_captain(int nation);  // oracle 0x483530 13B
int __fastcall sai_top_demand_arg(int nation);  // oracle 0x483540 14B
int __fastcall sai_top_demand_type(int nation);  // oracle 0x483550 28B
int __fastcall banker_request_throttle_passed(int nation);  // oracle 0x483570 36B
void __fastcall banker_stamp_request(int nation);  // oracle 0x4835A0 18B
void __fastcall aiscript_transports(int nation, int nValue);  // oracle 0x4835C0 32B
int __fastcall banker_at_define_max(int nation, int unitType);  // oracle 0x4835E0 103B
void __fastcall banker_set_has_upgrades(int nation, int on);  // oracle 0x483650 36B
void __fastcall banker_charge_unit_cost(int nation, int unitType);  // oracle 0x483680 68B
__declspec(noinline) void __fastcall banker_charge_plan_cost(int nation, int planType, int id);  // oracle 0x4836D0 116B
int __fastcall banker_default_build_on(int nation);  // oracle 0x483750 22B
void __fastcall aiscript_attack_clear(int nation, int bClearTimer);  // oracle 0x483770 64B
void __fastcall aiscript_default_build(int nation);  // oracle 0x4837B0 19B
int __fastcall banker_get_defaultmin(int nation);  // oracle 0x4837D0 14B
void __fastcall aiscript_defaultmin(int nation, int v);  // oracle 0x4837E0 13B
WORD * __fastcall aiscript_get_harass_need(int nation);  // oracle 0x4837F0 12B
int __fastcall banker_attack_target_area(int nation);  // oracle 0x483800 14B
AI_GenCmd __fastcall banker_get_gencmd(int nation);  // oracle 0x483810 29B
int __fastcall banker_attack_window_expired(int nation);  // oracle 0x483940 67B
void __fastcall aiscript_defaultbuild_off(int nation);  // oracle 0x483990 15B
void __fastcall banker_clear_harass_type(int nation, int unitType);  // oracle 0x4839A0 75B
void __fastcall banker_set_attack_target(int nation, int a2, int a3);  // oracle 0x483A60 72B
void __fastcall aiscript_set_gencmd(int nation, const AI_GenCmd *src);  // oracle 0x483AB0 20B
void __fastcall aiscript_quick_attack(int nation, int delay);  // oracle 0x483AD0 54B
__declspec(noinline) int __fastcall get_player_worker_type(int nPlayer);  // oracle 0x486070 37B
__declspec(noinline) int __fastcall get_player_refinery_type(int nPlayer);  // oracle 0x486100 36B
__declspec(noinline) int __fastcall aiscript_count_units(int unitType, int nation, int completed);  // oracle 0x4939E0 126B
__declspec(noinline) TAI_Captain * __fastcall banker_captain_for_request(int nation, const SAI_Request *pRequest);  // oracle 0x4B0930 33B
// town_clear_repair_worker -- 0x484F10, 33 B, oracle TU SAI_Build.cpp. Its one
// caller, CUnit::EnterTransport, calls it `mov ecx, ebx / call ...`: one
// register argument, no stack slots, and arg0 is a `CUnit *` by the callee's own
// dereferences at +0x64 (wType) and +0x134 (dwUnion134). void: EAX at the ret is
// the pointer loaded at +0x14 and the caller discards it. No explicit
// __fastcall, unlike most of this file: /Gr makes it the default, and one name
// spelled two ways across include roots gets no thunk at all.
void town_clear_repair_worker(CUnit *pUnit);

// The callees three bodies of ours reach that no header of ours declared. All
// three are spelled without an explicit __fastcall, for the reason
// town_clear_repair_worker above gives.

// oracle 0x487730 (152 B). Two register parameters and one pushed slot
// (`ret 4h`). The register roles come from banker_start_plan's call site: ECX
// still holds the nation and is reloaded from the global right after the call,
// which is only needed if the call clobbered it; EDX holds
// `lea edx, [edi+0Ch]` == &dwAvailOre and the single push is &dwAvailGas, so the
// two out-params are the banker's own DWORD resource fields.
void town_pending_costs(int nation, DWORD *pOre, DWORD *pGas);

// oracle 0x414180 (342 B). ECX and EDX live-in, no stack slots, and EAX consumed
// 32-bit at all three call sites. The second parameter is a BYTE because
// banker_start_plan loads it `mov dl, byte ptr [67F7A8h]`, an 8-bit move into DL
// rather than a widening into EDX.
unsigned count_food_available(int unitType, BYTE nation);

// oracle 0x482130 (301 B). __fastcall with `ret 8h`: two register and two stack
// parameters, all four stated by banker_demand_guard_unit's call site --
// ECX = nation, EDX = unitType, then `push 32h` and the
// banker_captain_for_request result.
//
// The third parameter's width is not derived: the callee reads its [ebp+8] slot
// 1-byte twice and 4-byte twice, an artifact of one `(BYTE)(count - 1)`
// expression, and every call site pushes an immediate, byte-identical either
// way. The fourth is spelled `int` rather than `TAI_Captain *` because the body
// forwards the slot untouched to sai_add_demand's fifth parameter, which this
// header declares `int`; the two are codegen-identical, so the spelling that
// needs no cast at the definition wins and banker_demand_guard_unit casts.
void AddPrereqUnitToAINeeds(int nation, int unitType, int a3, intptr_t a4);


// Four more callees ai_plan_next_nation and aiscript_harass_factor reach that no
// header of ours declared.

// oracle 0x4B2320 / 0x4AF930 / 0x4865B0, all three reached from
// ai_plan_next_nation, each taking the nation in ECX only and pushing nothing.
// town_player_tick is entered by a tail `jmp` after
// `mov ecx, dword ptr [0x67F7A8]`, which is legal only from a void caller with a
// matching frame.
void guard_update_posts(int nation);
void tai_captain_plan_all_needs(int nation);
void town_player_tick(int nation);

// oracle 0x48E080 (58 B). Two register arguments, no stack slots. The second is
// 32-bit on the callee's own evidence: it forwards the whole register with
// `mov eax, edx / mov ecx, eax / call GetRegionCenter`. A WORD spelling loses
// aiscript_harass_factor's `movzx eax / mov edx, eax` pair.
int ai_nearest_enemy_owner(int nation, int nTarget);

// oracle 0x481E40 (311 B). Declared ahead of its definition because
// sai_read_banker_data (0x481D90) sits earlier in oracle VA order and calls it.
int SAI_Banker_LoadFromSave(void *pFile);

// This TU's own bodies, declared because a hooked free function needs a header
// declaration, not because anything here calls them. Each signature is copied
// character-for-character from its definition in src117/lang/SAI_Banker.cpp: a
// declaration that disagrees with its definition is two mangled names and a
// LNK2019 at payload link. The __fastcall keyword is absent here as well, for
// town_clear_repair_worker's reason above.
void banker_start_plan();                                              // 0x4823D0
void banker_demand_guard_unit(int nation, SAI_Request *pRequest);             // 0x482A40
void banker_demand_structure(int nation, intptr_t a2, int unitType, int a4);      // 0x482AE0
void banker_demand_town_request(int nation, intptr_t a2, int a3, int unitType, int a5); // 0x482B60
void aiscript_start_campaign(int nation);                       // 0x482EE0
void banker_hold_ai_spells(int nation);                                    // 0x482EF0
void banker_add_current_resources(int nation, int nOre, int nGas); // 0x482F00
int  banker_worker_bonus_allowed(CUnit *pUnit);                                  // 0x482F40
// aiscript_has_campaign (0x4831E0) is deliberately not declared here:
// src117/h/CUnitSearches.h already declares it, beside the bodies that call it,
// and a name declared in two headers on the include path is ambiguous and gets
// no thunk at all.
void set_harass(unsigned nation, unsigned count, unsigned unitType); // 0x4839F0
int   sai_read_banker_data(void *pFile, WORD wVersion);          // 0x481D90
int   sai_write_banker_data(void *pFile);                        // 0x481F80
void  banker_demand_unit_for_captain(int nation, intptr_t a2, int unitType, int a4, int a5, int a6); // 0x482990
void  banker_resource_split(int nation, BYTE *pOrePct, BYTE *pGasPct);      // 0x483030
void  sai_banker_init();                                         // 0x4830D0
void  ai_plan_next_nation();                                              // 0x483400
void  aiscript_harass_factor(int nation, unsigned nFactor);      // 0x483830

// The two staged-cost readers town_pending_costs (SAI_Build.cpp) calls. Each
// takes the player in ECX, 32-bit, and returns the full EAX to every consuming
// caller. `unsigned`, and not ours to re-decide: SAI_Banker.cpp declares both
// `unsigned __fastcall` already, byte-for-byte as below, and its derivation is
// the three ai_stage_*_cost bodies that consume these, which end in
// `cmp / sbb eax,eax / inc eax` -- `return a >= b` unsigned -- with jnb/jb on
// every branch along the way.
unsigned __fastcall get_staged_ore_cost(int nation);
unsigned __fastcall get_staged_gas_cost(int nation);

// 0x408760 (8 B). Declared here, and not only locally in SAI_Banker.cpp as it
// once was, because a hooked free function must be declared in a header. The
// .cpp's own declaration is left in place: it is consulted only when no header
// declares the name, so that TU's compile is unchanged.
unsigned __fastcall get_staged_food_cost(int nation);

#endif  // OC117_SAI_BANKER_H
