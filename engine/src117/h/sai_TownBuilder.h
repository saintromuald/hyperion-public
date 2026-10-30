// sai_TownBuilder.h -- the AI town builder: the layer that decides where a
// computer player's next structure goes. It owns `town_plan`, the persistent
// per-town placement record, and the `TReserve` scratch grid the placement
// searches run over. Bodies are in sai_TownBuilder.cpp.
//
// Member functions live in their class header (src117/h/TReserve.h); this one
// owns the TU's free functions, because a hooked free function has to be
// declared in a header, and spells them without an explicit __fastcall, which
// /Gr already makes the default. sai_town_mineral_fields, sai_townbuilder_clear
// and sai_alert_frontier are declared in src117/h/sai.h instead and are not
// repeated here, since a second header declaring a name emits nothing at all.
//
// Each signature keeps its evidence: the `ret N`, which of ECX/EDX is a real
// input, each stack slot's width, and what the callers do with EAX.

#ifndef OC117_SAI_TOWNBUILDER_H
#define OC117_SAI_TOWNBUILDER_H

#include "platform.h"
#include "TReserve.h"
#include "rect16.h"
#include "pos.h"      // Hyperion: spos_t (stage E)

#include "dialog.h"   // point16_t, the canonical one; ai_get_build_location_near
                      // takes one by value, so a forward declaration will not do

class CUnit;    // sai_town_first_mineral returns one; the full type is not needed
                // here. `class`, not `struct` -- CUnit.h defines it as a class
                // and a mismatched key is C4099 in every TU that sees both.

// TTownPlan -- one entry of `town_plan` @0x6A2E88, the AI's persistent town
// placement record. 48 bytes, indexed 1-based: every accessor forms the address
// the same way, `lea eax, [ecx+ecx*2]` then `shl eax, 4h` then
// `<op> ptr [eax+6A2E88h]`, so the base folds into the displacement and the
// record is 0x30 wide.
//
// The element count is 250, from three agreeing numbers: sai_town_mineral_fields
// range-checks the index against 0 and 0xFA, so 1..249 are usable; 250 * 48 ==
// 0x2EE0 ends the array at 0x6A5D68, which sai_town_iter_next reads as an
// absolute DWORD and sai_townbuilder_init writes; and the save codec moves
// 0x2EE8 bytes from 0x6A2E88 -- the array plus two dwords -- ending where the
// next named global begins. Only the record's first 16 bytes are ever touched,
// and the rest keeps offset names.
struct TTownPlan {
    /*0x00*/ spos_t nX;             // sai_town_x: `movsx eax, word ptr [eax+6A2E88h]`; 4 bytes on x64 (stage E)
    /*0x02*/ spos_t nY;             // sai_town_y: `movsx eax, word ptr [eax+6A2E8Ah]`
    /*0x04*/ BYTE  bUnknown04;      // sai_town_mineral_fields returns it, zero-extended, range-checked
    /*0x05*/ BYTE  bUnknown05;      // sai_town_vespene_geysers returns it, zero-extended
    /*0x06*/ BYTE  bUnknown06;      // sai_townbuilder_init writes 1 here for every
                                    // start location (`mov byte ptr [eax+6h], 1h`,
                                    // three sites), and nothing else writes it. A
                                    // writer of a constant does not say what the
                                    // field means, so it keeps the offset spelling.
    /*0x07*/ BYTE  bFlags;          // sai_townbuilder_clear does `and byte ptr [...], -3h` (clear bit 1);
                                    // sai_town_iter_next skips an entry unless `(bFlags & 3) == 0`
    /*0x08*/ DWORD dwUnknown08;     // `mul dword ptr [esi+8h]` in both
                                    // sai_town_mineral_target and
                                    // sai_town_worker_target, each against the
                                    // 10624DD3h/`shr 5` reciprocal, MSVC's unsigned
                                    // divide by 500: the mul gives the width and the
                                    // sign. sai_recount_town_resources accumulates
                                    // into it (`add [esi+8h], eax`).
    /*0x0C*/ DWORD dwUnknown0C;     // `add dword ptr [esi+0Ch], eax` in
                                    // sai_recount_town_resources, the instruction
                                    // after the +0x08 accumulate, so a paired running
                                    // total. An accumulate gives the width, not the
                                    // meaning, so it keeps the offset spelling.
    /*0x10*/ BYTE  _unmapped_10[0x20];
};

// Hyperion: 1.17's layout, which x64 keeps but for the positions (stage E)
OC_X86_LAYOUT(sizeof(TTownPlan) == 0x30, "TTownPlan");
static_assert(offsetof(TTownPlan, nX)         == 0x00, "TTownPlan.nX");
OC_X86_LAYOUT(offsetof(TTownPlan, nY)         == 0x02, "TTownPlan.nY");
OC_X86_LAYOUT(offsetof(TTownPlan, bUnknown04) == 0x04, "TTownPlan.bUnknown04");
OC_X86_LAYOUT(offsetof(TTownPlan, bUnknown05) == 0x05, "TTownPlan.bUnknown05");
OC_X86_LAYOUT(offsetof(TTownPlan, bUnknown06) == 0x06, "TTownPlan.bUnknown06");
OC_X86_LAYOUT(offsetof(TTownPlan, bFlags)     == 0x07, "TTownPlan.bFlags");
OC_X86_LAYOUT(offsetof(TTownPlan, dwUnknown08) == 0x08, "TTownPlan.dwUnknown08");
OC_X86_LAYOUT(offsetof(TTownPlan, dwUnknown0C) == 0x0C, "TTownPlan.dwUnknown0C");

#define NUM_TOWN_PLANS 250

// 0x494FC0 ai_get_build_location (1058 B)
//   `ret Ch`; ECX read as an input, EDX never; free name -> one register argument
//     ECX is only ever forwarded to a callee, never read here
//   `ret Ch` -> 3 stack words
//     [ebp+08h] is read 2-byte x1, 4-byte x6
//     [ebp+0Ch] is loaded and then dereferenced -> a pointer
//     [ebp+10h] is read 4-byte x2
//   int: 2 consuming callers use the full EAX and none narrows it.
//   EDX carries a second register argument the callee ignores (/Gr fills EDX
//   before the body touches the stack), so `int` is a placeholder.
//   ECX is forwarded untouched, so the callee's contract has its width.
//   arg0's slot is read both wide and narrow: MSVC loads the whole dword even
//   for a narrow parameter, so `int` and a narrow spelling can be identical.
//   arg0's slot is written by the callee, a scratch use and not a reference.
int ai_get_build_location(CUnit *pUnit, int nType, mpoint_t pos, mpoint_t *pOut, unsigned nRadius);  // Hyperion: map points

// 0x495440 sai_read_town_data (17 B)
//   `ret`; ECX read as an input, EDX never; free name -> one register argument
//     ECX is read as 32 bits
//   no stack parameters
//   int: the one consuming caller uses the full EAX and none narrows it.
int sai_read_town_data(intptr_t nArg0);

// 0x495460 sai_write_town_data (17 B)
//   `ret`; ECX read as an input, EDX never; free name -> one register argument
//     ECX is read as 32 bits
//   no stack parameters
//   int: the one consuming caller uses the full EAX and none narrows it.
int sai_write_town_data(intptr_t nArg0);

// 0x495480 GetBunkerPlacement (297 B)
//   `ret`; EDX read before written -> a second register argument
//     ECX is read as 32 bits
//     EDX is dereferenced -> a pointer
//   no stack parameters
//   int: 2 consuming callers use the full EAX and none narrows it.
// arg1 is an out int *: the caller passes `lea edx,[ebp+10h]`, the address of a
// stack int.
int GetBunkerPlacement(int nPlayer, int *pnRegion);

// 0x4955B0 sai_town_plan_of_unit (272 B)
//   `ret`; ECX read as an input, EDX never; free name -> one register argument
//     ECX is read as 32 bits
//   no stack parameters
//   int: 2 consuming callers use the full EAX and none narrows it.
int sai_town_plan_of_unit(CUnit *pUnit);

// 0x4956C0 sai_alert_regions_outward (135 B)
//   `ret Ch`; EDX read before written -> a second register argument
//     ECX and EDX are read as 32 bits
//   `ret Ch` -> 3 stack words
//     [ebp+08h] is read 4-byte x1
//     [ebp+0Ch] is loaded and then dereferenced -> a pointer
//     [ebp+10h] is read 4-byte x2
//   void: EAX at the ret is inherited and no caller reads it.
void sai_alert_regions_outward(int nArg0, int nArg1, int nArg2, void *pArg3, int nArg4);

// 0x495750 sai_resource_in_patch (307 B)
//   `ret Ch`; EDX read before written -> a second register argument
//     ECX and EDX are read as 32 bits
//   `ret Ch` -> 3 stack words
//     [ebp+08h] through [ebp+10h] are each read 4-byte x1
//   int: 2 consuming callers use the full EAX and none narrows it.
//   The two trailing stack words are one rect16_t by value: sai_seed_town_plans pushes
//   a seed's rect straight from its record, the way sai_score_resource_patch's are pushed.
int sai_resource_in_patch(CUnit *pUnit, int nX, int nY, mrect_t rc);  // Hyperion: a box of map positions

// 0x495890 sai_seed_town_plans (787 B)
//   `ret`; ECX read as an input, EDX never; free name -> one register argument
//     ECX is only ever forwarded to a callee, never read here
//   no stack parameters
//   void: EAX at the ret is inherited and no caller reads it.
//   ECX is forwarded untouched, so the callee's contract has its width.
//   EDX carries a second register argument the callee ignores (/Gr fills EDX
//   before the body touches the stack), so `int` is a placeholder.
//   EDX IS read (`mov [ebp-20h], edx`): the per-unit kind bytes, walked in
//   step with the unit list and rewritten 1->3 / 2->4.
void sai_seed_town_plans(int nKind, BYTE *pKinds);

// 0x495BB0 ReservePlacement (325 B)
//   `ret Ch`; EDX read before written -> a second register argument
//     ECX and EDX are read as 32 bits
//   `ret Ch` -> 3 stack words
//     [ebp+08h] is read 4-byte x1
//     [ebp+0Ch] is read 4-byte x11
//     [ebp+10h] is read 4-byte x5
//   void: EAX at the ret is one of the two reserve calls' leftover and no
//   caller reads it.
//   arg2's slot is written by the callee, a scratch use and not a reference.
void ReservePlacement(int nArg0, intptr_t nArg1, intptr_t nArg2, mpoint_t pos, int nArg4);

// 0x495D40 AssignBuildableLocations (277 B)
//   `ret Ch`; EDX read before written -> a second register argument
//     ECX and EDX are read as 32 bits
//   `ret Ch` -> 3 stack words
//     [ebp+08h] is only ever read 1 byte(s) -> 8-bit (for a parameter the callee is the authority)
//     [ebp+0Ch] is read 4-byte x2
//     [ebp+10h] is read 4-byte x5
//   void: EAX is written on no path to a `ret`.
//   arg2's slot is written by the callee, a scratch use and not a reference.
void AssignBuildableLocations(int nArg0, TReserve *pReserve, BYTE bArg2, mpoint_t pos, int nArg4);

// 0x495E70 sai_nearest_marked_resource (570 B)
//   `ret 8h`; EDX read before written -> a second register argument
//     ECX and EDX are read as 32 bits
//   `ret 8h` -> 2 stack words
//     [ebp+08h] is read 4-byte x5
//     [ebp+0Ch] is loaded and then dereferenced -> a pointer
//   int: the one consuming caller uses the full EAX and none narrows it.
intptr_t sai_nearest_marked_resource(intptr_t nArg0, int nArg1, int nArg2, BYTE *pbFlags);

// 0x4960B0 ReserveStructure (225 B)
//   `ret 18h`; EDX read before written -> a second register argument
//     ECX and EDX are read as 32 bits
//   `ret 18h` -> 6 stack words
//     [ebp+08h] is read 4-byte x5
//     [ebp+0Ch] is read 4-byte x1
//     [ebp+10h] is read 4-byte x4
//     [ebp+14h] through [ebp+18h] are each read 4-byte x1
//     [ebp+1Ch] is read 4-byte x3
//   void: EAX at the ret is inherited and no caller reads it.
//   arg0's, arg2's and arg5's slots are written by the callee, a scratch
//   use and not a reference.
void ReserveStructure(int nArg0, int nArg1, int nArg2, int nArg3, int nArg4, intptr_t nArg5, mpoint_t pos, int nArg7);

// 0x4961A0 ReserveStructure_CUnit (100 B)
//   `ret Ch`; EDX read before written -> a second register argument
//     ECX is dereferenced -> a pointer
//     EDX is read as 32 bits
//   `ret Ch` -> 3 stack words
//     [ebp+08h] through [ebp+10h] are each read 4-byte x1
//   void: EAX is ReserveStructure's leftover and no caller reads it.
void ReserveStructure_CUnit(void *pArg0, int nArg1, intptr_t nArg2, mpoint_t pos, int nArg4);  // Hyperion: a map point, was packed in an int

// 0x496240 ai_get_build_location_at_unit (128 B)
//   `ret 8h`; EDX read before written -> a second register argument
//     ECX and EDX are read as 32 bits
//   `ret 8h` -> 2 stack words
//     [ebp+08h] through [ebp+0Ch] are each read 4-byte x1
//   int: the one consuming caller uses the full EAX and none narrows it.
int ai_get_build_location_at_unit(intptr_t nArg0, int nArg1, mpoint_t *pPos, int nArg3);  // Hyperion: the map point it writes

// 0x496320 sai_find_attack_route (394 B)
//   `ret 4h`; EDX read before written -> a second register argument
//     ECX and EDX are read as 32 bits
//   `ret 4h` -> one stack word
//     [ebp+08h] is loaded and then dereferenced -> a pointer
//   int: the one consuming caller uses the full EAX and none narrows it.
int sai_find_attack_route(int nNation, int nArg1, int *pnOut);

// 0x4964B0 sai_town_first_mineral (66 B)
//   `ret`; ECX read as an input, EDX never; free name -> one register argument
//     ECX is read as 32 bits
//   no stack parameters
//   int: the one consuming caller uses the full EAX and none narrows it.
// The callee states its own return: it walks CUnit::GetUsedList and returns the
// matching node in ESI, or 0.
CUnit *sai_town_first_mineral(int nArg0);

// 0x496500 sai_town_iter_begin (11 B)
//   `ret`  [tail:sai_town_iter_next]; ECX read as an input, EDX never; free name -> one register argument
//     ECX is dereferenced -> a pointer
//   no stack parameters
//   BOOL: no `ret` of its own; the tail jmp to sai_town_iter_next pins the return type.
//     1 consuming caller(s) use the full EAX and none narrows it -> BOOL (every ret materialises 0 or 1)
// The cursor is an `int *`: the body opens
// `mov dword ptr [ecx], 0h` and tail-jmps into sai_town_iter_next.
BOOL sai_town_iter_begin(int *pnTown);

// 0x496510 sai_town_gas_slots (374 B)
//   `ret 8h`; EDX read before written -> a second register argument
//     ECX and EDX are read as 32 bits
//   `ret 8h` -> 2 stack words
//     [ebp+08h] is loaded and then dereferenced -> a pointer
//     [ebp+0Ch] is loaded and then dereferenced -> a pointer
//   void: EAX at the ret is inherited and no caller reads it.
void sai_town_gas_slots(int nArg0, intptr_t nArg1, void *pArg2, void *pArg3);

// 0x496690 sai_town_worker_target (94 B)
//   `ret`; EDX read before written -> a second register argument
//     ECX is dereferenced -> a pointer
//     EDX is read as 32 bits
//   no stack parameters
//   int: 2 consuming callers use the full EAX and none narrows it.
int sai_town_worker_target(void *pArg0, int nArg1);

// 0x4966F0 sai_town_mineral_target (78 B)
//   `ret`; EDX read before written -> a second register argument
//     ECX is dereferenced -> a pointer
//     EDX is read as 32 bits
//   no stack parameters
//   int: the one consuming caller uses the full EAX and none narrows it.
int sai_town_mineral_target(void *pArg0, int nArg1);

// 0x496740 sai_town_iter_next (54 B)
//   `ret`; ECX read as an input, EDX never; free name -> one register argument
//     ECX is read as 32 bits
//   no stack parameters
//   BOOL: every ret materialises 0 or 1 and 1 callers use the full EAX.
// ECX is a pointer to the cursor, not the cursor itself:
// `mov edx, ecx` / `inc dword ptr [edx]` / `mov eax, dword ptr [edx]`.
BOOL sai_town_iter_next(int *pnTown);

// 0x496780 sai_region_route_cost (417 B)
//   `ret Ch`; EDX read before written -> a second register argument
//     ECX and EDX are read as 32 bits
//   `ret Ch` -> 3 stack words
//     [ebp+08h] is read 4-byte x2
//     [ebp+0Ch] is loaded and then dereferenced -> a pointer
//     [ebp+10h] is loaded and then dereferenced -> a pointer
//   int: the one consuming caller uses the full EAX and none narrows it.
// pArg3 and pArg4 are dereferenced as DWORDs (`mov dword ptr [ecx], edi`,
// `cmp dword ptr [ecx], eax`).
int sai_region_route_cost(int nArg0, int nArg1, int nArg2, int *pnArg3, int *pnArg4);

// 0x496930 sai_get_ground_distance (399 B)
//   `ret 8h`; EDX read before written -> a second register argument
//     ECX and EDX are read as 32 bits
//   `ret 8h` -> 2 stack words
//     [ebp+08h] is read 4-byte x4
//     [ebp+0Ch] is read 4-byte x2
//   int: 7 consuming callers use the full EAX and none narrows it.
int sai_get_ground_distance(int nArg0, int nArg1, int nArg2, int nArg3);

// 0x496AC0 sai_route_is_open (293 B)
//   `ret 4h`; EDX read before written -> a second register argument
//     ECX and EDX are read as 32 bits
//   `ret 4h` -> one stack word
//     [ebp+08h] is read 2-byte x1, 4-byte x1
//   int: 2 consuming callers use the full EAX and none narrows it.
//   arg0's slot is read both wide and narrow: MSVC loads the whole dword even
//   for a narrow parameter, so `int` and a narrow spelling can be identical.
int sai_route_is_open(int nArg0, int nArg1, int nArg2);

// 0x496BF0 sai_nearest_town_plan (134 B)
//   `ret 4h`; EDX read before written -> a second register argument
//     ECX and EDX are read as 32 bits
//   `ret 4h` -> one stack word
//     [ebp+08h] is read 4-byte x3
//   int: 2 consuming callers use the full EAX and none narrows it.
//   arg0's slot is written by the callee, a scratch use and not a reference.
int sai_nearest_town_plan(int nArg0, int nArg1, int nArg2);

// 0x496C80 sai_recount_town_resources (398 B)
//   `ret`; no register input, no stack parameters, plain `ret` -- every convention is byte-identical for a nullary function
//   no stack parameters
//   void: EAX at the ret is a leftover and no caller reads it.
//   EAX is defined on some paths to a ret and not others: BOOL with garbage,
//   or void with an incidental EAX.
void sai_recount_town_resources();

// 0x496E10 sai_mark_frontier_regions (266 B)
//   `ret 4h`; EDX read before written -> a second register argument
//     ECX and EDX are read as 32 bits
//   `ret 4h` -> one stack word
//     [ebp+08h] is read 4-byte x1
//   void: EAX is written on no path to a `ret`.
void sai_mark_frontier_regions(int nArg0, int nArg1, int nArg2);

// 0x496F40 sai_town_vespene_geysers (14 B)
//   `ret`; ECX read as an input, EDX never; free name -> one register argument
//     ECX is dereferenced -> a pointer
//   no stack parameters
//   int: the one consuming caller uses the full EAX and none narrows it.
int sai_town_vespene_geysers(int nTown);

// 0x496F50 sai_town_build_anchor (362 B)
//   `ret 4h`; EDX read before written -> a second register argument
//     ECX and EDX are read as 32 bits
//   `ret 4h` -> one stack word
//     [ebp+08h] is loaded and then dereferenced -> a pointer
//   void: EAX at the ret is an incidental write and no caller reads it.
//   Both pointers are short out-params; the fallback stores them with a 16-bit move.
void sai_town_build_anchor(int nTownPlan, spos_t *pOutX, spos_t *pOutY);  // Hyperion: positions

// 0x4970C0 sai_town_x (14 B)
//   `ret`; ECX read as an input, EDX never; free name -> one register argument
//     ECX is dereferenced -> a pointer
//   no stack parameters
//   int: 3 consuming callers use the full EAX and none narrows it.
int sai_town_x(int nTown);

// 0x4970D0 sai_town_y (14 B)
//   `ret`; ECX read as an input, EDX never; free name -> one register argument
//     ECX is dereferenced -> a pointer
//   no stack parameters
//   int: 3 consuming callers use the full EAX and none narrows it.
int sai_town_y(int nTown);

// 0x4970E0 sai_townbuilder_choose (631 B)
//   `ret 4h`; EDX read before written -> a second register argument
//     ECX and EDX are read as 32 bits
//   `ret 4h` -> one stack word
//     [ebp+08h] is read 4-byte x1
//   int: 2 consuming callers use the full EAX and none narrows it.
int sai_townbuilder_choose(int nArg0, intptr_t nArg1, int nArg2);

// 0x497360 sai_townbuilder_claim (31 B)
//   `ret`; ECX read as an input, EDX never; free name -> one register argument
//     ECX is dereferenced -> a pointer
//   no stack parameters
//   BOOL: every ret materialises 0 or 1 and 1 callers use the full EAX.
BOOL sai_townbuilder_claim(int nTown);

// 0x497390 sai_townbuilder_score (234 B)
//   `ret Ch`; EDX read before written -> a second register argument
//     ECX and EDX are read as 32 bits
//   `ret Ch` -> 3 stack words
//     [ebp+08h] is loaded and then dereferenced -> a pointer
//     [ebp+0Ch] is read 4-byte x2
//     [ebp+10h] is read 4-byte x2
//   void: EAX at the ret is veclen's leftover and no caller reads it.
void sai_townbuilder_score(int nArg0, int nArg1, void *pArg2, int nArg3, int nArg4);

// 0x497480 sai_score_resource_patch (418 B)
//   `ret 8h`; ECX read as an input, EDX never; free name -> one register argument
//     ECX is read as 32 bits
//   `ret 8h` -> 2 stack words
//     [ebp+08h] through [ebp+0Ch] are each read 4-byte x1
//   int: the one consuming caller uses the full EAX and none narrows it.
//   EDX carries a second register argument the callee ignores (/Gr fills EDX
//   before the body touches the stack), so `int` is a placeholder.
//   EDX carries nothing: the second argument is an 8-byte rect16_t, so /Gr
//   passes it on the stack (`push [esi+4]` / `push [esi]` in sai_seed_town_plans).
int sai_score_resource_patch(CUnit *pRefUnit, mrect_t rc);  // Hyperion: a box of map positions

// 0x4976D0 sai_reserve_walk_range (566 B)
//   `ret 4h`; EDX read before written -> a second register argument
//     ECX is dereferenced -> a pointer
//     EDX is read as 32 bits
//   `ret 4h` -> one stack word
//     [ebp+08h] is read 4-byte x3
//   void: EAX is written on no path to a `ret`.
void sai_reserve_walk_range(void *pArg0, int nArg1, mpoint_t pos);

// 0x497910 sai_reserve_safe_regions (153 B)
//   `ret 8h`; EDX read before written -> a second register argument
//     ECX and EDX are read as 32 bits
//   `ret 8h` -> 2 stack words
//     [ebp+08h] is read 4-byte x1
//     [ebp+0Ch] is read 4-byte x2
//   void: EAX is written on no path to a `ret`.
void sai_reserve_safe_regions(int nArg0, intptr_t nArg1, intptr_t nArg2, mpoint_t pos);

// 0x497A20 sai_townbuilder_init (879 B)
//   `ret`; no register input, no stack parameters, plain `ret` -- every convention is byte-identical for a nullary function
//   no stack parameters
//   void: EAX is sai_seed_town_plans's leftover and no caller reads it.
void sai_townbuilder_init();

// 0x497D90 sai_reserve_addon_space (340 B)
//   `ret 8h`; ECX read as an input, EDX never; free name -> one register argument
//     ECX is only ever forwarded to a callee, never read here
//   `ret 8h` -> 2 stack words
//     [ebp+08h] is loaded and then dereferenced -> a pointer
//     [ebp+0Ch] is read 4-byte x1
//   void: EAX at the ret is inherited and no caller reads it.
//   EDX carries a second register argument the callee ignores (/Gr fills EDX
//   before the body touches the stack), so `int` is a placeholder.
//   ECX is forwarded untouched, so the callee's contract has its width.
void sai_reserve_addon_space(int nArg0, intptr_t nArg1, void *pArg2, mpoint_t pos);

// 0x497F00 ReserveDefault (677 B)
//   `ret 8h`; ECX read as an input, EDX never; free name -> one register argument
//     ECX is only ever forwarded to a callee, never read here
//   `ret 8h` -> 2 stack words
//     [ebp+08h] is read 4-byte x1
//     [ebp+0Ch] is loaded and then dereferenced -> a pointer
//   void: EAX at the ret is memcpy's leftover and no caller reads it.
//   EDX carries a second register argument the callee ignores (/Gr fills EDX
//   before the body touches the stack), so `int` is a placeholder.
//   ECX is forwarded untouched, so the callee's contract has its width.
//   The fourth argument is a scalar: the body spills into its slot, which MSVC
//   does not do for a point16_t parameter.
void ReserveDefault(int nArg0, intptr_t nArg1, intptr_t nArg2, mpoint_t pos);  // Hyperion: a map point, was packed in an int

// 0x498230 sai_reserve_buildings (500 B)
//   `ret 8h`; ECX read as an input, EDX never; free name -> one register argument
//     ECX is only ever forwarded to a callee, never read here
//   `ret 8h` -> 2 stack words
//     [ebp+08h] is read 4-byte x1
//     [ebp+0Ch] is read 4-byte x2
//   void: EAX at the ret is inherited and no caller reads it.
//   EDX carries a second register argument the callee ignores (/Gr fills EDX
//   before the body touches the stack), so `int` is a placeholder.
//   ECX is forwarded untouched, so the callee's contract has its width.
void sai_reserve_buildings(int nArg0, intptr_t nArg1, intptr_t nArg2, mpoint_t pos);  // Hyperion: a map point, was packed in an int

// 0x498520 sai_reserve_pylon_placement (217 B)
//   `ret 4h`; ECX read as an input, EDX never; free name -> one register argument
//     ECX is only ever forwarded to a callee, never read here
//   `ret 4h` -> one stack word
//     [ebp+08h] is read 4-byte x2
//   void: EAX at the ret is inherited and no caller reads it.
//   EDX carries a second register argument the callee ignores (/Gr fills EDX
//   before the body touches the stack), so `int` is a placeholder.
//   ECX is forwarded untouched, so the callee's contract has its width.
void sai_reserve_pylon_placement(intptr_t nArg0, int nArg1, mpoint_t pos);

// 0x498600 sai_reserve_turret_placement (461 B)
//   `ret Ch`; ECX read as an input, EDX never; free name -> one register argument
//     ECX is only ever forwarded to a callee, never read here
//   `ret Ch` -> 3 stack words
//     [ebp+08h] is read 4-byte x2
//     [ebp+0Ch] is read 2-byte x1, 4-byte x1
//     [ebp+10h] is read 4-byte x2
//   void: EAX at the ret is inherited and no caller reads it.
//   EDX carries a second register argument the callee ignores (/Gr fills EDX
//   before the body touches the stack), so `int` is a placeholder.
//   ECX is forwarded untouched, so the callee's contract has its width.
//   arg1's slot is read both wide and narrow: MSVC loads the whole dword even
//   for a narrow parameter, so `int` and a narrow spelling can be identical.
// The third argument is a point16_t by value: the body reads its Y half as the
// dword at [ebp+0Ah], as sai_reserve_pylon_placement and sai_reserve_buildable_tiles do.
void sai_reserve_turret_placement(intptr_t nArg0, intptr_t nArg1, mpoint_t pos, int nArg3, int nArg4);

// 0x498820 sai_reserve_buildable_tiles (261 B)
//   `ret 4h`; ECX read as an input, EDX never; free name -> one register argument
//     ECX is only ever forwarded to a callee, never read here
//   `ret 4h` -> one stack word
//     [ebp+08h] is read 4-byte x3
//   void: EAX at the ret is inherited and no caller reads it.
//   EDX carries a second register argument the callee ignores (/Gr fills EDX
//   before the body touches the stack), so `int` is a placeholder.
//   ECX is forwarded untouched, so the callee's contract has its width.
void sai_reserve_buildable_tiles(int nArg0, intptr_t nArg1, mpoint_t pos);

// 0x498930 sai_reserve_template_spacing (192 B)
//   `ret 4h`; ECX read as an input, EDX never; free name -> one register argument
//     ECX is only ever forwarded to a callee, never read here
//   `ret 4h` -> one stack word
//     [ebp+08h] is never read by the callee
//   void: EAX at the ret is a leftover and no caller reads it.
//   EDX carries a second register argument the callee ignores (/Gr fills EDX
//   before the body touches the stack), so `int` is a placeholder.
//   ECX is forwarded untouched, so the callee's contract has its width.
//   arg0 is never read here, so only its 4-byte footprint is proven.
void sai_reserve_template_spacing(TReserve *pReserve, mpoint_t pos);

// 0x4962C0 ai_get_build_location_near (95 B). Its oracle TU is
//   sai_TownBuilder.cpp, so this header is its home; it was once declared
//   TU-locally in gametype.cpp, which a hooked row cannot be.
//     arg2 is a point16_t by value -- the caller supplies it as a zero-extended
//        dword in a register (`mov edx, edi`).
//     arg3 to arg6 are re-pushed verbatim into ai_get_build_location, and arg4
//        is the out position: sub_448210 passes `lea ecx,[ebp-4h]` and then
//        reads a WORD x at [ebp-4] and a WORD y at [ebp-2] from it.
//     The result is consumed by `test eax, eax`, so int.
int ai_get_build_location_near(CUnit *pUnit, int nUnitType, mpoint_t ptStart,
               mpoint_t *pPos, int nArg5, int nArg6);
#if defined(_WIN64) || defined(__LP64__)
// Hyperion: 1.17's points, for the callers that still take them.
inline int ai_get_build_location_near(CUnit *pUnit, int nUnitType, point16_t ptStart,
                      point16_t *pPos, int nArg5, int nArg6) {
  mpoint_t pt = *pPos;
  int nResult = ai_get_build_location_near(pUnit, nUnitType, mpoint_t(ptStart), &pt, nArg5, nArg6);
  *pPos = pt;
  return nResult;
}
#endif

// TAiPathReq -- the AI path-request block, 0x16C bytes, built on the caller's
// stack and handed to sub_491300. Four builders state the size: each one's
// `sub esp, N` minus its visible dword locals leaves 0x16C, and the four Ns
// differ by 0x1C, so a wrong size satisfies at most one of them
// (sai_get_ground_distance 0x178 - 0x0C, sai_region_route_cost 0x180 - 0x14,
// sai_nearest_marked_resource 0x190 - 0x24, sai_reserve_walk_range
// 0x194 - 0x28). Every named field lands on the same block offset in all four:
// the +0x86 count is [ebp-0F2h], [ebp-0FAh], [ebp-10Ah] and [ebp-10Eh].

// It is in the header rather than TU-local so that the layout checker sees its
// /*0xNN*/ annotations and offset asserts at all. sub_491300 has nine call sites
// across TUs, so the type will want a home of its own once a second TU builds
// one.
// Hyperion: the block is path_search_t (sai_PathFindArea.h), which holds two
// pointers, at +0x00 and +0x0C; this view is pointer-wide at both so that on x64
// every field stays on path_search_t's, as sai_TownBuilder.cpp asserts
//. DWORD at +0x00 and 12 bytes at +0x0C in 1.17.
struct TAiPathReq {
    /*0x000*/ DWORD_PTR dwFlags;    // `mov dword ptr [ebp-180h], 0h`      (sai_region_route_cost)
    // Hyperion: positions of 4 bytes on x64, as path_search_t's (h/pos.h, stage E)
    /*0x004*/ spos_t nFromX;        // `mov word ptr [ebp-174h], di`       (sai_get_ground_distance)
    /*0x006*/ spos_t nFromY;        // `mov word ptr [ebp-172h], si`       (sai_get_ground_distance)
    /*0x008*/ spos_t nToX;          // `mov word ptr [ebp-178h], ax`       (sai_region_route_cost)
    /*0x00A*/ spos_t nToY;          // `mov word ptr [ebp-176h], ax`       (sai_region_route_cost)
    /*0x00C*/ void *_unmapped_00C;
    /*0x010*/ BYTE  _unmapped_010[sizeof(mrect_t)];
    // Hyperion: path_search_t's ptCur, a point on x64 (stage E); 1.17 stored the
    // pair as one DWORD, dwToPacked
    /*0x018*/ mpoint_t ptToCur;     // the +0x08/+0x0A pair read back as one dword
                                    // (`mov eax,[ebp-178h]` / `mov [ebp-168h],eax`),
                                    // which is also what makes those two adjacent
                                    // shorts rather than one int.
    /*0x01C*/ DWORD dwUnknown01C;   // `mov dword ptr [ebp-164h], 0h`
    /*0x020*/ WORD  aRegions[0x33]; // `movzx edx, word ptr [ebp+esi*2-160h]`,
                                    // stride 2. The extent 0x33 is what runs up to
                                    // the count field, not something an instruction
                                    // states.
    /*0x086*/ short nRegionCount;   // `cmp ax, 2h` / `jnle`, so signed; decremented
                                    // in place in sai_get_ground_distance.
    /*0x088*/ BYTE  _unmapped_088[0xD0 + sizeof(mrect_t)];  // Hyperion: path_search_t's rcBound last (stage E)
    /*0x160*/ WORD  wFromRegion;    // `mov word ptr [ebp-20h], di`
    /*0x162*/ WORD  wToRegion;      // `mov word ptr [ebp-1Eh], bx`
    /*0x164*/ BYTE  _unmapped_164[0x08];   // inside the block by the size arithmetic and
                                           // untouched by all four builders.
};

OC_X86_LAYOUT(sizeof(TAiPathReq) == 0x16C, "TAiPathReq");
static_assert(offsetof(TAiPathReq, dwFlags)      == 0x000, "TAiPathReq.dwFlags");
OC_X86_LAYOUT(offsetof(TAiPathReq, nFromX)       == 0x004, "TAiPathReq.nFromX");
OC_X86_LAYOUT(offsetof(TAiPathReq, nFromY)       == 0x006, "TAiPathReq.nFromY");
OC_X86_LAYOUT(offsetof(TAiPathReq, nToX)         == 0x008, "TAiPathReq.nToX");
OC_X86_LAYOUT(offsetof(TAiPathReq, nToY)         == 0x00A, "TAiPathReq.nToY");
OC_X86_LAYOUT(offsetof(TAiPathReq, ptToCur)      == 0x018, "TAiPathReq.ptToCur");
OC_X86_LAYOUT(offsetof(TAiPathReq, dwUnknown01C) == 0x01C, "TAiPathReq.dwUnknown01C");
OC_X86_LAYOUT(offsetof(TAiPathReq, aRegions)     == 0x020, "TAiPathReq.aRegions");
OC_X86_LAYOUT(offsetof(TAiPathReq, nRegionCount) == 0x086, "TAiPathReq.nRegionCount");
OC_X86_LAYOUT(offsetof(TAiPathReq, wFromRegion)  == 0x160, "TAiPathReq.wFromRegion");
OC_X86_LAYOUT(offsetof(TAiPathReq, wToRegion)    == 0x162, "TAiPathReq.wToRegion");

// The callees this TU reaches that no header of ours declared.
//
// sub_491300 is not declared here: src117/h/sai_PathFindArea.h seats it as
// `BOOL sub_491300(path_search_t *)`, and two headers declaring one name with
// different signatures refuse generation outright (two identical declarations
// are harmless; it is disagreement that refuses).
//
// TAiPathReq above and path_search_t there are the same 0x16C oracle block under
// two names, both fully asserted. path_search_t is the survivor: it reads
// nRegions[50] plus nCursor at +0x84 off instructions where this file can only
// run aRegions[0x33] up to the count, and it names the tail this one leaves
// unmapped. Merging the two field vocabularies across 26 landed rows is a
// followup; until then the three call sites cast, which costs no code because
// the layouts are identical.

// tai_captain_region_enemy_strength -- 0x4AEAF0, 198 B, oracle TU
// TAI_GuardPost.cpp. `ret 4h` with ECX and EDX both read before written, so
// three arguments. All four call sites are inside sai_region_route_cost, and
// arg1 is fed `movzx edx, word ptr [...]`: an int parameter taking a promoted
// WORD, not a WORD parameter, since the callers decide where the callee widens
// anyway.
int tai_captain_region_enemy_strength(int nNation, int nRegion, int nFlags);

// ai_nearest_enemy_of_player -- 0x48DFF0, oracle TU SAI_Main.cpp. Declared here
// because sai_mark_frontier_regions calls it and a hooked row needs a header
// declaration. Returns a CUnit *.
CUnit *ai_nearest_enemy_of_player(int nArg0, int nArg1, int nArg2, int nArg3);

// find_choke_regions -- 0x40E190. Same reason; pwOut and pnOut are both written
// through.
int find_choke_regions(int nArg0, int nArg1, int nArg2, WORD *pwOut, int *pnOut, int nArg5);

// GetAreaIndex -- declared here for the same reason; no header had it.
WORD GetAreaIndex(WORD wRegion);

#endif  // OC117_SAI_TOWNBUILDER_H
