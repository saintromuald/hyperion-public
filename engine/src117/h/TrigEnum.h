// TrigEnum.h -- the interface of src117/lang/TrigEnum.cpp, the trigger
// enumeration family (oracle 0x4B95F0..0x4BA890). A trigger condition names a
// group -- a single player, or one of the pseudo-groups (foes, allies,
// neutrals, all, force 1..4, non-allied-victory) -- and every counting
// condition is evaluated by handing the same callback to the enumerator that
// expands that group into concrete nations. The callbacks (death_count,
// kill_count, score_count, units_count, owned_count, resource_count) are
// group-aware themselves, so the family is mutually recursive: a condition on
// all players re-enters its own accessor once per player.
//
// Include after the platform type scaffold (BOOL/BYTE/WORD/DWORD) and after
// <stddef.h>, which CUnit.h's static_asserts need.
#pragma once

#include "CUnit.h"
#include "rect16.h"
#include "pos.h"      // Hyperion: mrect_t

// MapRegion -- the trigger location record, 20 B. It is in this header rather
// than the .cpp because src117/lang/TrigAct.cpp needs the same record and the
// generated `extern MapRegion gMapRegions[255];` needs the type named on the
// include path. The stride is read arithmetically, `lea ecx, [eax+eax*4]` then
// `[ecx*4+534F40h]`: base + idx*5*4. The field order left/top/right/bottom
// comes from owned_count_atloc, which passes the four reads on as a rect16_t to
// find_foreach_unit. The bounds are DWORDs (act_center_view loads +0x00 at full
// width) and wFlags at +0x12 is a WORD. 0x534F52 is this record's own +0x12
// produced by the subscript, not a global of its own.
struct MapRegion {
  /*0x00*/ int  left;
  /*0x04*/ int  top;
  /*0x08*/ int  right;
  /*0x0C*/ int  bottom;
  // +0x10 is read nowhere. Its 2-byte size is forced by the 20-byte stride
  // minus the four dwords and wFlags, but nothing says what it holds.
  /*0x10*/ WORD _unread_10;
  /*0x12*/ WORD wFlags;   // movzx eax, word ptr [ecx*4+534F52h]
};
static_assert(sizeof(MapRegion)           == 0x14, "MapRegion stride (idx*5*4)");
static_assert(offsetof(MapRegion, left)   == 0x00, "MapRegion::left   -> rect.left");
static_assert(offsetof(MapRegion, top)    == 0x04, "MapRegion::top    -> rect.top");
static_assert(offsetof(MapRegion, right)  == 0x08, "MapRegion::right  -> rect.right");
static_assert(offsetof(MapRegion, bottom) == 0x0C, "MapRegion::bottom -> rect.bottom");
static_assert(offsetof(MapRegion, wFlags) == 0x12, "MapRegion::wFlags [+12h]");

// The three shapes in this family:
//
//   int  __fastcall <group>_enum      (int nGroup, WORD nParam, void *pCtx,
//                                      NationEnumCb pfnCb);            // ret 8h
//   int  __fastcall <group>_enum_count(int nGroup, WORD nParam, void *pCtx,
//                                      NationCountCb pfnCb, int nLimit);// ret 0Ch
//   int  __fastcall <accessor>        (DWORD nNation, WORD nParam, void *pCtx);
//                                                                      // ret 4h
//
// An enumerator's four parameters, two in registers and two on the stack, are
// what `ret 8h` means under /Gr; a counting variant adds the early-exit limit,
// pushed before the context, and ends `ret 0Ch`. The nation index is unsigned
// (`cmp` then `jnb`, never `jge`), so the walk variable is DWORD throughout.
// Only group_enum reads the first parameter, the group id minus 0x11 as a force
// number; the other five ignore it, since all six share one signature.

// The plain enumeration callback -- `cb(nation /*ecx*/, param /*edx*/, ctx)`.
// The param is a WORD: every accessor switches on `movzx eax, si`, and the
// trigger actions forward DX to the enumerators unwidened.
typedef int (__fastcall *NationEnumCb)(DWORD nNation, WORD nParam, void *pCtx);

// Hyperion: the enumerators hand each callback a slot, and the callbacks
// read their first argument as a trigger's player code first, where 13 to 26 are groups
// (13 the current player, 14 foes, 17 all players, 18 to 21 forces). A slot past 11, which
// only a Hyperion game has, is passed with this tag, so a callback takes it as a slot and
// not as the group of the same number. A 1.17 game never has one, so it passes nothing
// tagged and plays as 1.17.
#define TRIG_SLOT_TAG 0x10000u
static inline DWORD TrigSlotTag(DWORD nSlot) { return nSlot >= 12 ? nSlot | TRIG_SLOT_TAG : nSlot; }

// Hyperion: a Hyperion map's triggers name a slot past 11 as the player
// code TRIG_HX_CODE + slot (hxm-format.md, Triggers), a code 1.17 never writes. A Hyperion
// game turns it into the tagged slot when it reads the map (TriggerHxCodes).
#define TRIG_HX_CODE 0x100u

// The early-exit variant -- `cb(nation, param, ctx, limit)`. The limit rides
// along so a recursive expansion stops at the same total the outer walk does.
typedef int (__fastcall *NationCountCb)(DWORD nNation, WORD nParam, void *pCtx,
                                        int nLimit);

// The unit-finder callback -- `cb(pUnit /*ecx*/, ctx /*edx*/)`. find_foreach_unit
// ends `ret 4h` with the context as its one stack argument, and owned_count_atloc
// pops nothing after the call, so the callee retires it.
typedef int (__fastcall *FindUnitCb)(CUnit *pUnit, void *pCtx);

// External callees, each called by a landed body below.

// oracle 0x445690. Both operands arrive in ECX and EDX and the body never
// touches the stack, and TrigEnum_HeightMatches sets both with a full-width
// `movsx`, so both are int. It returns 0, 1 or 2, and the one caller treats
// anything else as no match.
int __fastcall get_map_height(int x, int y);

// oracle 0x4B3160 (11 B) / 0x4B3170 (191 B), TU Teams.cpp. The first is
// `mov dword ptr [ecx], -1h / jmp nation_find_next`, so ECX is a pointer to a
// single DWORD cursor and the two share one walk. Both return the nation in
// EAX, or 8 when the walk is exhausted, so every caller's loop guard is `< 8`
// and never a count.
DWORD __fastcall nation_find_first(int *pnCursor);
DWORD __fastcall nation_find_next(int *pnCursor);

// oracle 0x4BDA70 (6 B). The nation whose triggers are being evaluated. The
// result goes into ECX as a player index at every call site, and the compiler
// cannot CSE the call, so the bodies below spell it two and three times per
// iteration as the oracle does.
int __fastcall TriggerPlayer(void);

// oracle 0x443560 (19 B), TU forces.cpp. Whole body:
//   cmp ecx, 8h / jl +3 / xor al, al / ret
//   lea eax, [ecx+ecx*8] / mov al, byte ptr [eax*4+6726D2h] / ret
// The compare is signed, so the one register argument is an int, unlike the
// nation walk variable in this file; the BYTE load is what the callers' `movzx
// eax, al` consumes, so the return is a BYTE.
BYTE __fastcall GetPlayerForce(int nNation);

// oracle 0x443110. Calls find_units_in_rect with the rect in ECX, then walks
// the returned CUnit * list calling cb(pUnit, pCtx). `ret 4h`, so the callee
// retires the context. It returns the last unit it looked at; no caller here
// reads it.
CUnit *__fastcall find_foreach_unit(const mrect_t *pArea, FindUnitCb pfnCb,
                                    void *pCtx);
#if defined(_WIN64) || defined(__LP64__)
// Hyperion: 1.17's box, for the callers that still take it.
inline CUnit *find_foreach_unit(const rect16_t *pRect, FindUnitCb pfnCb, void *pCtx) {
  mrect_t rc = *pRect;
  return find_foreach_unit(&rc, pfnCb, pCtx);
}
#endif

// The landed bodies, in oracle VA order, which is this TU's declaration order:
// under /Ox a TU's functions land in .text in declaration order, and
// src117/lang/TrigEnum.cpp keeps that sequence. Held bodies keep their slot in
// it and are simply absent from this header.

// 0x4B95F0 (107 B). Returns the caller's mask ANDed with the one bit that names
// this unit's height band, or 0 when the map height is outside 0..2:
//   ground (dwFlags & 4 clear): h=0 -> 0x01, h=1 -> 0x02, h=2 -> 0x04
//   air    (dwFlags & 4 set)  : h=0 -> 0x08, h=1 -> 0x10, h=2 -> 0x20
// The second parameter is a WORD: units_count_atloc sets it with `mov dx, word
// ptr [edx]`, and an int parameter would have been a `movzx`.
int __fastcall TrigEnum_HeightMatches(CUnit *pUnit, WORD wMask);

// 0x4B9660 (94 B). Every nation: the 0..7 cursor walk, then 8..11 unconditionally.
int __fastcall all_enum(int nGroup, WORD nParam, void *pCtx, NationEnumCb pfnCb);

// 0x4B96C0 (107 B). all_enum plus the early exit on total == limit.
int __fastcall all_enum_count(int nGroup, WORD nParam, void *pCtx,
                              NationCountCb pfnCb, int nLimit);

// 0x4B9730 (97 B). Nations the trigger player is allied with (alliance == 2),
// excluding itself. No 8..11 tail -- the neutral slots are never allies.
int __fastcall allies_enum(int nGroup, WORD nParam, void *pCtx, NationEnumCb pfnCb);

// 0x4B97A0 (113 B).
int __fastcall allies_enum_count(int nGroup, WORD nParam, void *pCtx,
                                 NationCountCb pfnCb, int nLimit);

// 0x4B9820 (8 B). Whole body: `mov byte ptr [6CA6D0h], 0h / ret`. Invalidates
// the cached-enumeration mode so the next count refills the tables.
void __fastcall clear_enum_cache(void);

// 0x4B9B10 (97 B). Alliance == 0.
int __fastcall foes_enum(int nGroup, WORD nParam, void *pCtx, NationEnumCb pfnCb);

// 0x4B9B80 (113 B).
int __fastcall foes_enum_count(int nGroup, WORD nParam, void *pCtx,
                               NationCountCb pfnCb, int nLimit);

// 0x4B9C60 (91 B). The one that reads argument 1: force = nGroup - 0x11,
// compared against GetPlayerForce(nation).
int __fastcall group_enum(int nGroup, WORD nParam, void *pCtx, NationEnumCb pfnCb);

// 0x4B9CC0 (107 B).
int __fastcall group_enum_count(int nGroup, WORD nParam, void *pCtx,
                                NationCountCb pfnCb, int nLimit);

// 0x4B9E60 (115 B). "Non-allied victory": every nation that is not the trigger
// player and is not mutually allied with it. The two alliance reads index the
// table in opposite directions, which is what makes the guard an `||`.
int __fastcall nava_enum(int nGroup, WORD nParam, void *pCtx, NationEnumCb pfnCb);

// 0x4B9EE0 (131 B).
int __fastcall nava_enum_count(int nGroup, WORD nParam, void *pCtx,
                               NationCountCb pfnCb, int nLimit);

// 0x4B9F70 (97 B). Alliance == 1.
int __fastcall neutrals_enum(int nGroup, WORD nParam, void *pCtx, NationEnumCb pfnCb);

// 0x4B9FE0 (113 B).
int __fastcall neutrals_enum_count(int nGroup, WORD nParam, void *pCtx,
                                   NationCountCb pfnCb, int nLimit);

// 0x4BA350 (184 B). Refills the three cached count tables for one map region,
// memoised on (mode == 2, same region). The argument is a BYTE: `mov bl, cl`
// then `movzx eax, bl` -- an int argument would index the region table with the
// full 32-bit register.
void __fastcall owned_count_atloc(BYTE nRegion);

// 0x4BA830 (66 B). The per-unit visitor of the location sweep: four successive
// rejects, then the two counters. EDX points at the caller's height mask.
// Always returns 0 -- find_foreach_unit ignores the result.
int __fastcall units_count_atloc(CUnit *pUnit, WORD *pwHeightMask);

// 0x4B9830 (312 B). Counts one unit, and the units it carries, as completed.
void __fastcall count_completed(CUnit *pUnit);

// 0x4B9970 (99 B). Counts one unit into the cached game counts. The TU's own
// caller calls an internal twin, count_game_local.
void __fastcall count_game(CUnit *pUnit);

// Hook targets that were once declared only in the .cpp: a hook row takes a
// free function's address, which needs a declaration in a header. The
// signatures are copied from the definitions character for character.

// 0x4B99E0 (220 B).
int __fastcall death_count(DWORD nNation, WORD nUnitType, void *);

// 0x4B9C00 (81 B).
void __fastcall TrigEnum_PrepCounts(void);

// 0x4B9D30 (220 B).
int __fastcall kill_count(DWORD nNation, WORD nUnitType, void *);

// 0x4BA260 (174 B).
int __fastcall owned_count(DWORD nNation, WORD, void *pCtx);

// 0x4BA410 (185 B).
int __fastcall resource_count(DWORD nNation, WORD nResType, void *);

// 0x4BA510 (276 B).
int __fastcall score_count(DWORD nNation, WORD nScoreType, void *);

// 0x4BA680 (336 B).
int __fastcall units_count(DWORD nNation, WORD nUnitType, void *pCtx);

