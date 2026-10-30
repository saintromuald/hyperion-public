// CUnitSearches.cpp -- the unit-search block: the per-candidate filter predicates
// every generic unit search dispatches through, the AI spell-target dispatchers
// that pick a filter and a radius, the rect sweep that counts a unit type, the
// visibility transition that hides a unit's sprites, and the weapon-minimum-range
// cache the attack-target search reads.
//
// Bodies are in the classic game's own order. The gaps in that order are this TU's unported
// functions, declared in src117/h/CUnitSearches.h.

// Include order matters. src117/gen/globals117.h is generated with no includes of
// its own and relies on its includer having BYTE, DWORD and its rows' struct
// types in scope, so it comes last; net_cmds.h comes before it, because
// `extern PlayerData gPlayerData[8];` needs that type complete.
#include "oc_data.h"  // Hyperion: OC_VA
#include "../h/CUnitSearches.h"   // ours: this TU's own header -- the free
                                  // functions below, the unported free callees
                                  // they reach, and the predicate contract

#include "../h/CUnit.h"           // ours: the unit fields the bodies read, their
                                  // unported CUnit callees, and the generic
                                  // searches the dispatchers forward to
                                  // (sub_442BC0 / FindNearestInRect)

#include "../h/CSprite.h"         // ours: wPosX (+0x14) / wPosY (+0x16), the
                                  // flags byte (+0x0E), bVisibilityFlags (+0x0C,
                                  // the per-player mask sub_433DF0 bit-tests),
                                  // and Hide / Show / RemoveCircle

#include "../h/TrigEnum.h"        // ours: find_foreach_unit and its FindUnitCb,
                                  // the rect sweep sub_432070 forwards to;
                                  // rect16_t comes with it

#include "../h/net_cmds.h"        // ours: PlayerData, gPlayerData's element type,
                                  // which sub_432510 indexes by player and whose
                                  // nType it compares

#include "../h/TrigAct.h"         // ours: net_rand
#include "../h/CUnitCombat.h"     // ours: ai_target_unreachable
#include "../h/sai_path.h"        // ours: sai_path_compute_distance
#include "../h/collide.h"         // ours: sub_412C90
#include "../h/CTarget.h"         // ours: veclen
#include "../h/statbtn.h"         // ours: TechUseAllowed
#include "../h/COrder.h"          // ours: COrder::target
#include "../h/CUnitTerran.h"     // ours: sub_442B60
#include <string.h>
#include <stdio.h>   // Hyperion: HYPERION_FIND_CHECK's log
#include <stdlib.h>  // Hyperion: getenv
#include "../gen/globals117.h"
#include "../h/playervision.h"  // Hyperion: the slots past 7
#include "../h/hxrules.h"       // Hyperion: HxRules

// The globals this TU touches that src117/gen/globals117.h does not emit yet,
// spelled here in the same dual `#ifdef OC_DLL_MODE` form the generated header
// uses, each behind its own `#ifndef` so a name that lands later takes over
// without disturbing the rest. The `dword_*` and `stru_*` names are the sheet's
// placeholders, reproduced as they are rather than given a semantic name.

// sgMapPixRect_g -- WORD[4] @0x543228, the map viewport pixel rect (cellscrl.cpp).
// All three CUnit::FindNearestInRect call sites below push its address as the
// fixed search-area blob and never dereference it, so what this TU needs is the
// pointer and not the layout.
#ifdef OC_DLL_MODE
#define pHarvestSearchData (reinterpret_cast<const mrect_t *>(OC_VA(0x543228)))
#else
#ifndef sgMapPixRect_g
extern mrect_t sgMapPixRect_g;  // Hyperion: a box of map positions
#endif
#define pHarvestSearchData (&sgMapPixRect_g)
#endif

// dword_665D18 -- DWORD @0x665D18, a full dword at sub_432510's read
// (`cmp dword ptr [0x665D18], 0h`). CUnit::FindFreeMineral (0x431FE0) is the
// writer: it stores its own second argument here whole, alongside 0x665D10 and
// 0x665D14, before registering sub_432510 as the search predicate. So this is
// that search's configuration, read back out of a static rather than threaded
// through the callback.
#ifndef dword_665D18
#ifdef OC_DLL_MODE
#define dword_665D18 (*reinterpret_cast<DWORD *>(OC_VA(0x665D18u)))
#else
extern DWORD dword_665D18;
#endif
#endif

// dword_665D20 -- DWORD @0x665D20, CUnit::AISetMinRange's only output: all four
// of its exits end in a full-dword store here. It caches the minimum of this
// unit's ground and air weapon minimum ranges.
#ifndef dword_665D20
#ifdef OC_DLL_MODE
#define dword_665D20 (*reinterpret_cast<DWORD *>(OC_VA(0x665D20u)))
#else
extern DWORD dword_665D20;
#endif
#endif

// wpn_minrange_g -- DWORD[130] @0x5252B0, weapons.dat's minimum-range column
// (arr/weapons.cpp). Element width 4, from CUnit::AISetMinRange's `mov eax, dword
// ptr [eax*4+5252B0h]`; the extent is bracketed by the next name,
// wpn_maxrange_g @0x5254B8, 0x208 bytes on, which is 130 entries of 4 and
// weapons.dat's entry count.
#ifndef wpn_minrange_g
#ifdef OC_DLL_MODE
#define wpn_minrange_g (reinterpret_cast<DWORD *>(OC_VA(0x5252B0u)))
#else
extern DWORD wpn_minrange_g[130] OC_UNALIGNED;
#endif
#endif

// gdwLocalMask -- DWORD @0x672668 (net_data.cpp). The local player's slot
// bitmask. CUnit::sub_4325A0, the only site here, loads it once into EAX and uses
// that register for both of its tests, which is where the DWORD comes from: the
// registry records the name and the TU but no type.
#ifndef gdwLocalMask
#ifdef OC_DLL_MODE
#define gdwLocalMask (*reinterpret_cast<DWORD *>(OC_VA(0x672668u)))
#else
extern DWORD gdwLocalMask;
#endif
#endif

// dword_665D28 -- DWORD @0x665D28 (CUnitSearches.cpp), 4 B, bracketed by
// dword_665D2C at +4. Both referents, ai_count_collateral_energy and
// ai_count_collateral_shields, do `add dword ptr [0x665D28], eax`: it is an
// accumulator, and both counters return FALSE unconditionally so the search
// visits every candidate. The unported ai_test_best_nuke_target (0x4335B0) zeroes
// it, runs the sweep and compares the total against 0x320. What the accumulated
// quantity means is not derived.
#ifndef dword_665D28
#ifdef OC_DLL_MODE
#define dword_665D28 (*reinterpret_cast<DWORD *>(OC_VA(0x665D28u)))
#else
extern DWORD dword_665D28;
#endif
#endif

// dword_665D30 -- DWORD @0x665D30 (CUnitSearches.cpp). Its four writers are this
// TU's only accesses: AIFindBroodlingTarget, AIFindIrradiateTarget,
// AIFindParalyzeTarget and AIFindPsiStormTarget each open with a full-dword store
// of the incoming mode flag, ahead of the frame and ahead of the flag test.
// Nothing here reads it back, so its role is not derived.
#ifndef dword_665D30
#ifdef OC_DLL_MODE
#define dword_665D30 (*reinterpret_cast<DWORD *>(OC_VA(0x665D30u)))
#else
extern DWORD dword_665D30;
#endif
#endif

// dword_665D34 -- DWORD @0x665D34 (sheet row 815). AIFindNukeTargetInArea below
// writes it full width (`mov dword ptr [0x665D34], 0h`) and reads it back the
// same way. Its true type is `CUnit *`: the writer that puts a non-zero value in
// it is the unported filter ai_test_best_nuke_target_in_area, storing the
// candidate it was just handed, and AIFindNukeTargetInArea merges it with a
// CUnit* by `cmovnz`. The registry's DWORD is kept and the read is cast.
#ifndef dword_665D34
#ifdef OC_DLL_MODE
#define dword_665D34 (*reinterpret_cast<DWORD_PTR *>(OC_VA(0x665D34u)))
#else
extern DWORD_PTR dword_665D34;
#endif
#endif

// stru_665D38 -- rect16_t @0x665D38, eight bytes rather than two independent
// dwords: the sheet names 0x665D38 and then nothing until 0x665D40, so it
// brackets one 8-byte object, and `stru_` says that object is an aggregate. Its
// only reader, the unported filter ai_test_best_nuke_target_in_area (0x433620),
// bounds-checks a sprite position against four words at +0x00 left, +0x02 top,
// +0x04 right, +0x06 bottom -- rect16_t field for field -- and compares them
// `jl`/`jnle`, signed, as rect16_t's members are. So 0x665D3C is this object's
// `right` and needs no name of its own.
#ifndef stru_665D38
#ifdef OC_DLL_MODE
#define stru_665D38 (*reinterpret_cast<mrect_t *>(OC_VA(0x665D38u)))
#else
extern mrect_t stru_665D38;  // Hyperion: a box of map positions
#endif
#endif

// The AI area-survey scorers' three slots.

// dword_665D08 -- @0x665D08 (sheet row 805, CUnitSearches.cpp), 4 B, bracketed by
// dword_665D10 at +8 with nothing named between. It is a CUnit*, at both ends of
// one caller and filter pair: ai_find_nearest_enemy_type stores its own incoming
// unit there whole and sub_432250 reads it straight back into ECX as
// CUnit::IsEnemyUnit's `this`. The stash exists because sub_442D40's filter
// signature has no slot for the searching unit.
#ifndef dword_665D08
#ifdef OC_DLL_MODE
#define dword_665D08 (*reinterpret_cast<CUnit **>(OC_VA(0x665D08u)))
#else
extern CUnit *dword_665D08;
#endif
#endif

// dword_665D24 -- DWORD @0x665D24 (sheet row 811). A full dword at its writers
// and at ai_query_area_enemy_hp's compare, not at its narrowest read:
// ai_query_area_ally_hp below reads only the low byte, because the value is
// consumed there as CanTargetWithWeapon's BYTE weapon id -- a narrowing read at
// the point of use, not a byte field.
#ifndef dword_665D24
#ifdef OC_DLL_MODE
#define dword_665D24 (*reinterpret_cast<DWORD *>(OC_VA(0x665D24u)))
#else
extern DWORD dword_665D24;
#endif
#endif

// dword_665D2C -- DWORD @0x665D2C (sheet row 813). ai_query_area_ally_hp's only
// output and its one referent: `add dword ptr [0x665D2C], eax`. The ally-side
// twin of dword_665D28's enemy-side accumulator; the two filters add into
// different slots so a scorer can weigh friendly against enemy damage from a
// single sweep.
#ifndef dword_665D2C
#ifdef OC_DLL_MODE
#define dword_665D2C (*reinterpret_cast<DWORD *>(OC_VA(0x665D2Cu)))
#else
extern DWORD dword_665D2C;
#endif
#endif

// find_first_unit_near -- oracle 0x442B20, oracle TU find.cpp, unported, declared
// here with C linkage. The one 0x442B20 is reached through prototypes that
// genuinely disagree: 16-bit coordinates and a two-argument predicate here and in
// CUnitTSpells.cpp / CUnitZSpells.cpp, widened `int` ones and a one-argument
// bounce filter in CBullet.cpp, each correct at its own call sites. They share one
// 20-byte fastcall footprint, so under `extern "C"` they decorate alike and one
// declaration serves. Keep this view TU-local, or a TU that includes both headers
// declares incompatible C overloads.
//
// x and y are 16 bits at this TU's callers, which emit bare 16-bit register
// writes a 32-bit parameter cannot produce.
// Hyperion: the definition (find.cpp) reads the position as ints, and this
// TU's callers pass it as 16 bits. The view takes the definition's types, which
// the x64 build needs, and the macro narrows the arguments as 1.17's did
//.
extern "C" CUnit *__fastcall find_first_unit_near(int x, int y, int nRadius, CUnitSearchPredicate pfnFilter, CUnit *pContext);
#define find_first_unit_near(a0, a1, a2, a3, a4) (find_first_unit_near)(PosU(a0), PosU(a1), (a2), (a3), (a4))

// sub_431980.
// The powerup-pickup filter: five guards -- gulUnitTypeFlags[wType] bit 3, the "carries a
// powerup" bit CUnit.h names; dwFlags bit 0; UF_Burrowed; bit 0x40000000; and pPowerup --
// then a same-player check against the searching unit. The five are one `&&` with
// `return 0;` as the tail, which puts the shared failure epilogue last as 1.17 has it.
// 0xD7 (215), the unit type the inner test exempts, is not named.
BOOL __fastcall sub_431980(CUnit *pUnit, CUnit *pOther)
{
  if ((gulUnitTypeFlags[pUnit->wType] & 8) &&
      (pUnit->dwFlags & 1) &&
      !(pUnit->dwFlags & 0x10) &&
      !(pUnit->dwFlags & 0x40000000) &&
      !pUnit->pPowerup)
  {
    if (pOther->wType != 0xD7 || pOther->bPlayer != pUnit->bPlayer)
      return 1;
  }

  return 0;
}

// CUnit::sub_431A90.
// "Is this unit usable": unlike the search predicates around it, it takes no second CUnit*,
// just `this` in ECX. Seven must-be-true guards joined into one `&&` ending in `return 1;`,
// which keeps the shared failure epilogue last. The four disable-style timers are tested in
// 1.17's own order -- Lockdown (+0x117), Stasis (+0x119), Paralyze (+0x124), then bUnion123
// (+0x123) -- not by offset.
BOOL CUnit::sub_431A90()
{
  if ((gulUnitTypeFlags[wType] & 0x8000) &&
      (dwFlags & 1) &&
      !(dwFlags & 0x400) &&
      !bLockdownTimer &&
      !bStasisTimer &&
      !bParalyzeTimer &&
      !UnitMyopia(this).Any())  // Hyperion: bUnion123, and the blinders past 7 (stage D)
    return 1;

  return 0;
}

// CUnit::sub_431F00.
// "Find somewhere to return this harvest to." Five unit types -- 0x83, 0x9A, 0x6A, 0x84,
// 0x85 -- are polled for a completed instance owned by this unit's player, and only if one
// exists is the search run, with SearchResourceDepot as the filter and sgMapPixRect_g as the
// area. The player is cached in an int local, loaded once for the five calls.
// ai_find_nearest_enemy_type.
// The type search's launcher: stash this unit in dword_665D08 so the filter can reach it,
// then sweep the whole map rectangle for a unit of the given type the stashed unit counts as
// an enemy. Free, since the pointer is never dereferenced here. wType is 16 bits, widened only
// for sub_442D40's fifth slot. The coordinates are locals, and the y read is widened to WORD
// (CSprite::wPosY is signed, wPosX not) so it loads with movzx.
CUnit *__fastcall ai_find_nearest_enemy_type(CUnit *pUnit, WORD wType)
{
  CSprite *pSprite = pUnit->pSprite;
  int y, x;

  dword_665D08 = pUnit;
  y = PosU(pSprite->wPosY);
  x = pSprite->wPosX;

  return sub_442D40(x, y, &sgMapPixRect_g,
                    reinterpret_cast<BOOL(__fastcall *)(CUnit *, int)>(sub_432250),
                    wType);
}

// Hyperion: by Hyperion's rules, the nearest depot a worker with cargo goes back to is
// found among its own player's units (revision 13):
// the same tests as SearchResourceDepot's, the units 1.17's finder holds, 1.17's distance, and
// on a tie the unit of the lower slot. 1.17's search scans outward through the finder's two
// orderings and so visits every unit in a column and a row of the map through the worker,
// most of them other players'; on a map of 1,024 tiles and 10,000 units it was half the
// units' update. The reach test, the costly one, is asked only of a depot nearer than the
// best so far.
static CUnit *HxNearestDepot(CUnit *pWorker)
{
  const int nX = PosS(pWorker->pSprite->wPosX);
  const int nY = PosS(PosU(pWorker->pSprite->wPosY));
  CUnit *pBest = NULL;
  int nBest = 0;
  for (CUnit *p = CUnit::GetPlayerUnitList(pWorker->bPlayer); p != NULL; p = p->pPlayerUnitNext) {
    // in the finder, as 1.17's search sees only those (CUnit::BeginTracking)
    if (p == pWorker || *reinterpret_cast<int *>(reinterpret_cast<BYTE *>(p) + offsetof(CUnit, _unmapped_138) + 0x04) < 0)
      continue;
    if (!p->IsResourceDest())
      continue;
    const int nDist = veclen(p->pSprite->wPosX, nX, p->pSprite->wPosY, nY);
    if (pBest != NULL && (nDist > nBest || (nDist == nBest && p > pBest)))
      continue;
    if (!SearchResourceDepot(p, pWorker))
      continue;
    pBest = p;
    nBest = nDist;
  }
  return pBest;
}

CUnit *CUnit::sub_431F00()
{
  int nPlayer = bPlayer;

  if (!count_get_completion(0x83, nPlayer) &&
      !count_get_completion(0x9A, nPlayer) &&
      !count_get_completion(0x6A, nPlayer) &&
      !count_get_completion(0x84, nPlayer) &&
      !count_get_completion(0x85, nPlayer))
    return 0;

  if (HxRules())
    return HxNearestDepot(this);
  return FindNearestInRect(pHarvestSearchData, SearchResourceDepot, this);
}

// CUnit::sub_432060.
//
// One instruction, a tail jump to CUnit::sub_431B70, so it shares that function's whole
// signature: `this` passes through in ECX and its result is this one's.
CUnit *CUnit::sub_432060()
{
  return sub_431B70();
}

// CUnit::sub_432070.
// Count or find one unit type inside a rect, by forwarding to find_foreach_unit with
// test_has_type_in_rect as the callback and the type as the context word. A __thiscall member
// whose body ignores `this`. The parameter is a full dword, narrowed afterwards with
// `(void *)(WORD)nUnitType` because the callback reads a WORD. The reinterpret_cast on the
// callback lets test_has_type_in_rect keep its WORD parameter.
CUnit *CUnit::sub_432070(const rect16_t *pArea, DWORD nUnitType)
{
  return find_foreach_unit(pArea, reinterpret_cast<FindUnitCb>(test_has_type_in_rect),
                           (void *)(WORD)nUnitType);
}

// SearchResourceDepot.
// The filter CUnit::sub_431F00 above searches with: pUnit is the candidate depot, which must
// be a resource-return destination owned by the same player as pHarvester, and only then is
// pHarvester asked whether it can path to pUnit's sprite position. A free function. The third
// check is nested inside the first two, not written flat, so it branches to the shared
// epilogue; the sprite position goes through explicit locals, y before x.

// The accumulators. Both add into dword_665D28 and return FALSE unconditionally,
// so the dispatching search visits every candidate and never stops early. That is
// 1.17's design, kept rather than corrected.

// oracle 0x432090 (103 B). The enemy-side twin of ai_query_area_ally_hp: the same
// dword_665D24 weapon id, the same callees, the same `&&` chain whose four
// rejects jump forward onto the epilogue the fall-through also ends on.
//
// It adds two things over the ally twin. A self-comparison guard, so the source
// unit does not score itself. And a second accumulation arm selected on
// `dword_665D24 == 0x3C`, which takes the raw hit points at +0x08 and does
// `add eax, 0FFh / sar eax, 8h` -- the x256 round-up, (hp + 255) >> 8 rather than
// a plain shift, and arithmetic because lHitPoints is a signed `long`. MSVC
// tail-duplicates the epilogue for that arm instead of jumping to the shared one;
// the source is a plain if/else with one trailing `return FALSE;`.
BOOL __fastcall ai_query_area_enemy_hp(CUnit *pCandidate, CUnit *pSource)
{
  if (pCandidate != pSource &&
      !(pCandidate->dwFlags & 0x4000000) &&
      pSource->CanTargetWithWeapon(static_cast<BYTE>(dword_665D24), pCandidate) &&
      pSource->IsEnemyUnit(pCandidate))
  {
    if (dword_665D24 == 0x3C)
      dword_665D28 += (pCandidate->lHitPoints + 0xFF) >> 8;
    else
      dword_665D28 += pCandidate->GetToughness();
  }

  return FALSE;
}

// ai_query_area_ally_hp.
// Not really a predicate: it adds each passing candidate's toughness into dword_665D2C and
// always answers FALSE, so the sweep visits every candidate. Kept, not corrected. One `&&`
// chain, not three guards. The byte cast on dword_665D24 is a narrowing read at the use: the
// slot holds CUnit::CanTargetWithWeapon's BYTE weapon id, and other bodies use it whole.
BOOL __fastcall ai_query_area_ally_hp(CUnit *pCandidate, CUnit *pSource)
{
  if (!(pCandidate->dwFlags & 0x4000000) &&
      pSource->CanTargetWithWeapon(static_cast<BYTE>(dword_665D24), pCandidate) &&
      !pSource->IsEnemyUnit(pCandidate))
    dword_665D2C += pCandidate->GetToughness();

  return FALSE;
}

// oracle 0x432150 (142 B). The filter ai_test_best_nuke_target address-takes. Its
// only exit is `xor eax, eax`, so it too never stops the sweep early.
//
// The unit-type flags word is a local, not three reloads: 1.17 loads it once and
// then tests DL twice across the body, the low-byte form MSVC uses for a mask
// that fits a byte. The shield term is an if/else into a local rather than a `?:`
// folded into the add, and `sar` on +0x60 is why lShields -- spelled DWORD in
// CUnit.h to match its store width -- is read through an `int` here: its value is
// signed.
BOOL __fastcall ai_query_collateral_damage(CUnit *pCandidate, CUnit *pSource)
{
  WORD   wType;
  DWORD  dwTypeFlags;
  int    nShields;

  if ((pCandidate->dwFlags & 0x4000000) || !pSource->IsEnemyUnit(pCandidate))
    return FALSE;

  wType = pCandidate->wType;
  dwTypeFlags = gulUnitTypeFlags[wType];

  if ((dwTypeFlags & 8) || !(pCandidate->dwFlags & 0x20000))
  {
    if (gbUnitHasShields[wType])
      nShields = static_cast<int>(pCandidate->lShields) >> 8;
    else
      nShields = 0;

    dword_665D28 += ((pCandidate->lHitPoints + 0xFF) >> 8) + nShields;
  }

  if (dwTypeFlags & 1)
  {
    if (pCandidate->sub_431A90() || wType == 0x92 || wType == 0x67)
      dword_665D28 = 0x320;
  }

  return FALSE;
}

// sub_432250.
// The filter ai_find_nearest_enemy_type registers: accept a candidate whose type matches the
// searched-for one, or any type when the sentinel 0xE4 (228, one past the last units.dat row)
// is asked for, and which the stashed searcher counts as an enemy. One `&&`-guarded return.
// The result is compared, `return f(...) != FALSE;`, which normalises it to 0 or 1.
BOOL __fastcall sub_432250(CUnit *pCandidate, WORD wType)
{
  if (pCandidate->wType != wType && wType != 0xE4)
    return FALSE;

  return dword_665D08->IsEnemyUnit(pCandidate) != FALSE;
}

BOOL __fastcall SearchResourceDepot(CUnit *pUnit, CUnit *pHarvester)
{
  if (pUnit->IsResourceDest() && pUnit->bPlayer == pHarvester->bPlayer)
  {
    upos_t y = pUnit->pSprite->wPosY;
    upos_t x = pUnit->pSprite->wPosX;

    if (sai_path_can_reach_target(pHarvester, x, y))
      return 1;
  }

  return 0;
}

// test_has_type_in_rect.
//
// Register-shaped like a member but free: the unit-search callback sub_432070 hands to
// find_foreach_unit. A plain equality test of CUnit::wType, zero-extended to a full BOOL.
BOOL __fastcall test_has_type_in_rect(CUnit *pUnit, WORD wType)
{
  return pUnit->wType == wType;
}

// sub_432510.
// A search predicate: CUnit::FindFreeMineral pushes its address as a dispatcher's filter, so
// it reads its configuration out of dword_665D18. A free function. The shape is
// `if (A) { if (B && C && D) return 0; }`, not a flat `&&` chain, so the last test can still
// branch out; the tail is a bare `return !x;`, the boolean-NOT fold SearchResourceDepot has
// to avoid.
BOOL __fastcall sub_432510(CUnit *pUnit, CUnit *pHarvester)
{
  if (!sub_432420(pUnit, pHarvester))
    return 0;

  if (dword_665D18 && gPlayerData[pHarvester->bPlayer].nType == 1 && pUnit->bUnionD9)
    return 0;

  return !pUnit->sub_4242F0();
}

// CUnit::sub_432560.
// Hides the unit: sub_4A7B80 first, gated on sprite flags bit 3 (read through the whole-byte
// arm, since only bits 1 and 2 are named), then RemoveCircle and Hide on the unit's own
// sprite, then the subunit's sprite if there is one. That last call is a tail jump, so it is
// the final statement.
void CUnit::sub_432560()
{
  if (pSprite->flags.b & 8)
    sub_4A7B80();

  pSprite->RemoveCircle();
  pSprite->Hide();

  if (subunit)
    subunit->pSprite->Hide();
}

// CUnit::sub_4325A0.
// The visibility transition: recompute who can see this unit, diff the new mask against
// dwUnionE4, tell RemoveTargetReferencesForPlayers about the players that lost sight of it,
// and, only if the local player's own bit moved, show or hide the unit's sprites.
// dwOld == 0x80000000 means "never computed": dwUnionE4 is then seeded with the inverse of
// the fresh mask, so every player reads as changed. Both sprite paths are tail jumps.
void CUnit::sub_4325A0()
{
  // Hyperion: a game with Hyperion's slots takes the same steps over a set of every
  // slot, whose slots past 31 are kept beside this DWORD (stage D)
  if (g_hxPlayers) {
    DetectionUpdateHx(this);
    return;
  }
  DWORD dwNew = sub_4316B0();
  DWORD dwOld = dwUnionE4;

  if (dwOld == dwNew)
    return;

  if (dwOld == 0x80000000)
    dwUnionE4 = (~dwNew) & 0xFF;

  DWORD dwChanged = dwUnionE4 ^ dwNew;
  dwUnionE4 = dwNew;

  DWORD dwRemoved = (~dwNew) & dwChanged;
  if (dwRemoved)
    RemoveTargetReferencesForPlayers(dwRemoved);

  if (!(dwChanged & gdwLocalMask))
    return;

  if (dwUnionE4 & gdwLocalMask) {
    pSprite->Show();
    if (subunit)
      subunit->pSprite->Show();
  } else {
    sub_432560();
  }
}

// sub_432870.
// Address-taken by AIFindRepairTarget as a search filter, so it has the family signature,
// BOOL __fastcall (CUnit *candidate, CUnit *source). A guard sequence, not an `&&` chain.
// The two flag tests share one load, hence the local. The sprite position is loaded movzx
// then widened: the (WORD) on wPosY gives that load, with CSprite.h's `short` untouched. y is
// loaded first.

// oracle 0x432620 (41 B). Address-taken only: sub_432BD0 pushes it as
// CUnit::sub_442BC0's filter. All three guards jump forward to the one trailing
// `xor eax,eax / pop esi / ret`, so this is a single `&&` chain ending
// `return TRUE;` with one trailing `return FALSE;`. `cmp esi, edx` has pCandidate
// as the destination, so it is the parameter named first.
BOOL __fastcall sub_432620(CUnit *pCandidate, CUnit *pSource)
{
  if (pCandidate != pSource &&
      pSource->IsEnemyUnit(pCandidate) &&
      pCandidate->CanAttack())
    return TRUE;

  return FALSE;
}

// oracle 0x432650 (132 B). Address-taken only: AIFindBroodlingTarget pushes it as
// a search filter, and that dispatcher is also what stashes the mode argument in
// dword_665D30 for this predicate to read back.
//
// gulUnitTypeFlags[wType] is loaded whole into a callee-saved register because it
// is masked twice, so it is a DWORD local rather than two table reads. The tail
// `mov ecx,6 / add eax,-7Bh / cmp ecx,eax / sbb eax,eax / inc eax` is MSVC's
// branchless unsigned range test, computing (unsigned)(bOrderId - 0x7B) <= 6,
// bOrderId in [0x7B,0x81]; the constant has to be the compare's destination to
// put (6 < v) in CF, so it is materialised in ECX.
BOOL __fastcall ai_test_best_broodling_target(CUnit *pCandidate, CUnit *pSource)
{
  int   nType;
  DWORD ulFlags;

  if (!sub_433DF0(pSource, pCandidate))
    return FALSE;

  if (!pSource->CanTargetWithWeapon(0x39, pCandidate))
    return FALSE;

  nType = pCandidate->wType;
  ulFlags = gulUnitTypeFlags[nType];
  if (ulFlags & 0x40)
    return FALSE;

  if (dword_665D30 == 0 &&
      pCandidate->GetToughness() < 100 &&
      !(ulFlags & 8) &&
      nType != 0x1E &&
      nType != 5 &&
      nType != 0x22)
  {
    int nOrder = pCandidate->bOrderId;
    return nOrder >= 0x7B && nOrder <= 0x81;
  }

  return TRUE;
}

// oracle 0x4327E0 (131 B). Address-taken only: AIFindCureTarget pushes it as
// CUnit::sub_442BC0's filter. The failure epilogue comes first with two backward
// jumps into it, so those are separate `if (...) return FALSE;` statements. The
// shield term is a real if/else, and both shifts are arithmetic (`sar`), so
// lShields -- a DWORD in CUnit.h -- is shifted as a signed int here.
BOOL __fastcall ai_test_best_cure_target(CUnit *pCandidate, CUnit *pSource)
{
  int nShields;

  if (pCandidate->bPlayer != pSource->bPlayer)
    return FALSE;

  if (!pSource->CanTargetWithWeapon(0x66, pCandidate))
    return FALSE;

  nShields = gbUnitHasShields[pCandidate->wType]
               ? (static_cast<int>(pCandidate->lShields) >> 8)
               : 0;

  if (((pCandidate->lHitPoints + 0xFF) >> 8) + nShields <= 60)
    return FALSE;

  if (!pCandidate->bEnsnareTimer &&
      !pCandidate->bBloodBoilTimer &&
      !pCandidate->bIrradiateTimer &&
      !pCandidate->bLockdownTimer &&
      !pCandidate->bParalyzeTimer &&
      !pCandidate->bCrunchyCount)
    return FALSE;

  return TRUE;
}

BOOL __fastcall sub_432870(CUnit *pCandidate, CUnit *pSource)
{
  DWORD ulTypeFlags;
  CSprite *pSprite;
  upos_t wY, wX;

  if (pCandidate->bPlayer != pSource->bPlayer)
    return FALSE;

  ulTypeFlags = gulUnitTypeFlags[pCandidate->wType];
  if (ulTypeFlags & 0x08)
    return FALSE;

  if (ulTypeFlags & 0x01)
    return FALSE;

  if (pCandidate->pOrderTarget != pSource)
    return FALSE;

  if (!UnitCanReachGroundTarget(pCandidate, pSource))
    return FALSE;

  pSprite = pCandidate->pSprite;
  wY = PosU(pSprite->wPosY);
  wX = pSprite->wPosX;

  return pSource->ErrorRClick(0x22, pCandidate, PosS(wX), PosS(wY),
                              NULL) != 0;
}

// The banner the bodies below refer to: each matches in the proof form, where the
// `__restrict` on the sprite pointer keeps the first accumulator store below the
// folded coordinate loads. In the shipped form the accumulator is a dereferenced
// literal rather than a symbol, `__restrict` no longer holds that store down, and
// it moves one `push` earlier -- the same addresses baked at different offsets.

// oracle 0x432950 (130 B).
//
// Shared-exit-last: all four rejects jump forward to the one
// `pop edi / xor eax,eax / pop esi / ret` epilogue that trails the success
// path, so this is a single `&&` chain with one trailing `return FALSE;`,
// not four separate guards.
BOOL __fastcall ai_test_best_energy_target(CUnit *pCandidate, CUnit *pSource)
{
  if (sub_433DF0(pSource, pCandidate) &&
      (gulUnitTypeFlags[pCandidate->wType] & 0x200000) &&
      !(pCandidate->dwFlags & 0x40000000) &&
      pSource->CanTargetWithWeapon(0x21, pCandidate))
  {
    CSprite * __restrict pSprite = pCandidate->pSprite;
    upos_t y = PosU(pSprite->wPosY);
    upos_t x = pSprite->wPosX;

    dword_665D28 = 0;
    dword_665D24 = 0x21;
    find_first_unit_near(x, y, 0xA0, ai_count_collateral_energy, pSource);

    return dword_665D28 >= 0xC8;
  }

  return FALSE;
}

// oracle 0x4329E0 (114 B).
//
// Shared-exit-first: the `pop edi / xor eax,eax / pop esi / ret` epilogue is
// emitted immediately after the first guard and the other two jump backward
// into it, which is the separate-`if` shape, not an `&&` chain.
BOOL __fastcall ai_test_best_shields_target(CUnit *pCandidate, CUnit *pSource)
{
  if (!sub_433DF0(pSource, pCandidate))
    return FALSE;

  if (gbUnitHasShields[pCandidate->wType] == 0)
    return FALSE;

  if (!pSource->CanTargetWithWeapon(0x21, pCandidate))
    return FALSE;

  {
    CSprite * __restrict pSprite = pCandidate->pSprite;
    upos_t y = PosU(pSprite->wPosY);
    upos_t x = pSprite->wPosX;

    dword_665D28 = 0;
    dword_665D24 = 0x21;
    find_first_unit_near(x, y, 0xA0, ai_count_collateral_shields, pSource);
  }

  return dword_665D28 >= 0xC8;
}

// sub_432A60.
// The attack-target filter, the widest of the search predicates: a candidate qualifies only
// if it is not the searcher, is an enemy, is visible to the searcher's player when it carries
// either 0x300 concealment flag, is a legal target, and passes CUnit::IsUnitInWeaponRange.
// EDX is the searching unit and ECX the candidate. The visibility check is nested inside the
// 0x300 test, and the tail is an explicit `!= 0` compare.

// oracle 0x4328D0 (127 B). Six guards, every one jumping forward to the single
// trailing epilogue, so one `&&` chain. `cmp dword ptr [esi+5Ch], eax` compares
// pOrderTarget against the 0 IsEnemyUnit left in EAX -- a reuse of the constant,
// not evidence about the operand. `mov eax,[esi+8] / add eax,0FFh /
// and eax,-100h` rounds the x256 hit-point field up to a whole point before
// comparing against 0x4B00 == 75*256, and the compare is signed, matching
// CUnit::lHitPoints' `long`.
BOOL __fastcall ai_test_best_matrix_target(CUnit *pCandidate, CUnit *pSource)
{
  CSprite *pSprite;

  if (pCandidate->wDefMatrixHitPoints == 0 &&
      !pSource->IsEnemyUnit(pCandidate) &&
      pCandidate->pOrderTarget != NULL &&
      !(pCandidate->dwFlags & 0x100) &&
      pCandidate->GetLife() >= 100 &&
      ((pCandidate->lHitPoints + 0xFF) & -0x100) <= 0x4B00)
  {
    upos_t wY, wX;

    pSprite = pCandidate->pSprite;
    wY = pSprite->wPosY;
    wX = pSprite->wPosX;
    return pSource->sub_434130(6, pCandidate, 0xE4,
                               PosS(wX),
                               PosS(wY),
                               NULL) != FALSE;
  }

  return FALSE;
}

BOOL __fastcall sub_432A60(CUnit *pTarget, CUnit *pUnit)
{
  if (pTarget == pUnit)
    return FALSE;

  if (!pUnit->IsEnemyUnit(pTarget))
    return FALSE;

  if ((pTarget->dwFlags & 0x300) &&
      !UnitDetectedBy(pTarget, pUnit->bPlayer))  // Hyperion: every slot (stage D)
    return FALSE;

  if (!pUnit->CanTarget(pTarget, 1))
    return FALSE;

  return pUnit->IsUnitInWeaponRange(pTarget) != 0;
}

// A forwarding helper for the proximity search. Its parameters are evaluated
// before the call is set up, which is what leaves the sprite X in EAX and
// moves it to ECX afterwards.
static __forceinline CUnit *srch_near(upos_t x, upos_t y, int nRadius,
                                      CUnitSearchPredicate pfn, CUnit *pCtx)
{
  return find_first_unit_near(x, y, nRadius, pfn, pCtx);
}

// oracle 0x4317A0 (36 B). Whether any unit within 0x200 of this one passes
// sub_432A60.
BOOL CUnit::HasEnemyInWeaponRange()
{
  return srch_near(pSprite->wPosX, pSprite->wPosY, 0x200, sub_432A60, this) != NULL;
}

// oracle 0x431C20 (27 B). The first unit within 0x18 of this one that passes
// sub_431980.
CUnit *CUnit::sub_431C20()
{
  return srch_near(pSprite->wPosX, pSprite->wPosY, 0x18, sub_431980, this);
}

// CUnit::sub_432BB0.
//
// A thin search entry point: the same sgMapPixRect_g search area
// CUnit::sub_431F00 uses, with sub_432E70 as the filter and `this` as the
// context. The incoming ECX is pushed as the context argument and left in place
// as the `this` for the member call, which is what an unqualified member call on
// the same object compiles to.
CUnit *CUnit::sub_432BB0()
{
  return FindNearestInRect(pHarvestSearchData, sub_432E70, this);
}

// sub_432BD0.
//
// CUnit::sub_432BB0's shape through the radius search (CUnit::sub_442BC0, 0x100)
// instead of the fixed-area one, and free rather than a member: the incoming
// pointer is pushed as the trailing context and used as the callee's `this`,
// where a member spelling would pass `this` implicitly and could not also produce
// the push.
CUnit *__fastcall sub_432BD0(CUnit *pUnit)
{
  return pUnit->sub_442BC0(0x100, sub_432620, pUnit);
}

// AIFindBroodlingTarget.
// The first of the AI spell-target dispatchers: store the mode flag in dword_665D30, then
// forward to the radius search with a range the flag picks. Two spellings the whole family
// shares: the range is a local, not two returns, and the ternary stays written out even where
// both arms are equal (0x800 here, on the aiscript_has_campaign path), since MSVC folds the
// value but keeps the call.
CUnit *__fastcall AIFindBroodlingTarget(CUnit *pUnit, int nFlags)
{
  int nRange;

  dword_665D30 = nFlags;
  if (nFlags)
    nRange = 0x120;
  else
    nRange = aiscript_has_campaign(pUnit->bPlayer) ? 0x800 : 0x800;

  return pUnit->sub_442BC0(nRange, ai_test_best_broodling_target, pUnit);
}

// ai_test_bunker_target.
//
// test_has_type_in_rect's shape: ECX and EDX in, bare `ret`, no stack arguments.
// ECX is the candidate being tested, EDX the unit that wants to enter it, and the
// predicate is true only when the candidate is a Bunker owned by the same player.
// 0x7D is the Bunker units.dat id, a bare literal because unittypes.h is
// deliberately not on this file's include path.
BOOL __fastcall ai_test_bunker_target(CUnit *pBunker, CUnit *pUnit)
{
  return pBunker->wType == 0x7D && pBunker->bPlayer == pUnit->bPlayer;
}

// Hyperion: the medic's two searches for a unit to restore accept only a unit of the
// medic's own player under an effect, and an AI medic asks both every frame it is not busy:
// the parasite one walks the whole map, about 23,000 steps of the finder, and the two were 88%
// of the finder's steps in a 64-player game. Their filters only read, so when no unit
// on the player's list is under such an effect neither can find anything, and the answer is
// none without the walk. HYPERION_FIND_CHECK=1 walks anyway and logs to hyperion_find_check.log
// any unit it finds where the list said none.
static bool HxFindCheck(void)
{
  static const bool s_bOn = getenv("HYPERION_FIND_CHECK") != NULL;
  return s_bOn;
}

static CUnit *HxFindChecked(const char *pszWhat, CUnit *pFound)
{
  static unsigned long s_nChecked, s_nDiffer;
  ++s_nChecked;
  if (pFound != NULL)
    ++s_nDiffer;
  if ((pFound != NULL && s_nDiffer <= 1000) || (s_nChecked & 4095) == 0) {
    if (FILE *f = fopen("hyperion_find_check.log", "a")) {
      if (pFound != NULL)
        fprintf(f, "%s: found unit %u where the list said none\n", pszWhat, pFound->GetUnitID());
      else
        fprintf(f, "%lu checked, %lu differ\n", s_nChecked, s_nDiffer);
      fclose(f);
    }
  }
  return pFound;
}

// Whether a unit of pSource's player is under what the cure filter (bParasite false) or the
// parasite cure filter (true) asks for, before either's other tests.
static bool HxOwnUnitUnder(const CUnit *pSource, bool bParasite)
{
  for (const CUnit *p = CUnit::GetPlayerUnitList(pSource->bPlayer); p != NULL; p = p->pPlayerUnitNext) {
    if (bParasite ? UnitParasite(p).Any() || p->bParalyzeTimer
                  : p->bEnsnareTimer || p->bBloodBoilTimer || p->bIrradiateTimer || p->bLockdownTimer ||
                        p->bParalyzeTimer || p->bCrunchyCount)
      return true;
  }
  return false;
}

// AIFindCureTarget.
//
// sub_432BD0's twin: the same radius search with a wider radius (0x400) and the
// cure-target filter. The body never reads the second parameter; the declaration
// in src117/h/CUnitSearches.h says why it is declared anyway.
CUnit *__fastcall AIFindCureTarget(CUnit *pUnit, int nFlags)
{
  if (!HxOwnUnitUnder(pUnit, false))  // Hyperion: none to find
    return HxFindCheck() ? HxFindChecked("cure", pUnit->sub_442BC0(0x400, ai_test_best_cure_target, pUnit)) : NULL;
  return pUnit->sub_442BC0(0x400, ai_test_best_cure_target, pUnit);
}

// AIFindMatrixTarget.
//
// AIFindBroodlingTarget's range-selecting forward without the leading
// dword_665D30 store, which is the six bytes between 62 and 56, and with both
// ternary arms equal at 0x800: the same discarded-call fold, so the ternary is
// kept for the same reason.
CUnit *__fastcall AIFindMatrixTarget(CUnit *pUnit, int nFlags)
{
  int nRange;

  if (nFlags)
    nRange = 0x120;
  else
    nRange = aiscript_has_campaign(pUnit->bPlayer) ? 0x800 : 0x800;

  return pUnit->sub_442BC0(nRange, ai_test_best_matrix_target, pUnit);
}

// AIFindDisruptorTarget.
//
// The first of the family whose two range arms differ, 0x1000 and 0x800, so the ternary
// compiles to the neg/sbb/and/add mask form instead of folding away.
CUnit *__fastcall AIFindDisruptorTarget(CUnit *pUnit, int nFlags)
{
  int nRange;

  if (nFlags)
    nRange = 0x120;
  else
    nRange = aiscript_has_campaign(pUnit->bPlayer) ? 0x1000 : 0x800;

  return pUnit->sub_442BC0(nRange, ai_test_best_disruptor_target, pUnit);
}

// AIFindEMPTarget.
// The widest of the family: two searches, the second run only if the first found nothing and
// the flag is zero, one `&&`. The range has two uses here, so MSVC joins the two arms with a
// `jmp` instead of duplicating the call as the single-use siblings do. Both arms are 0x800.
CUnit *__fastcall AIFindEMPTarget(CUnit *pUnit, int nFlags)
{
  int nRange;
  CUnit *pTarget;

  if (nFlags)
    nRange = 0x120;
  else
    nRange = aiscript_has_campaign(pUnit->bPlayer) ? 0x800 : 0x800;

  pTarget = pUnit->sub_442BC0(nRange, ai_test_best_shields_target, pUnit);
  if (!pTarget && !nFlags)
    pTarget = pUnit->sub_442BC0(nRange, ai_test_best_energy_target, pUnit);
  return pTarget;
}

// AIFindEnsnareTarget.
//
// AIFindDisruptorTarget's mask form with other constants and another filter:
// `and eax, 400h / add eax, 400h`, so the arms are 0x800 and 0x400.
CUnit *__fastcall AIFindEnsnareTarget(CUnit *pUnit, int nFlags)
{
  int nRange;

  if (nFlags)
    nRange = 0x120;
  else
    nRange = aiscript_has_campaign(pUnit->bPlayer) ? 0x800 : 0x400;

  return pUnit->sub_442BC0(nRange, ai_test_best_ensnare_target, pUnit);
}

// AIFindHallucinateTarget.
//
// The guarded forward. The `jz` skips the `xor eax,eax / ret` pair, so the search
// runs when the flag is zero and the early NULL is the nonzero arm -- the inverse
// of the reading the branch sense invites, and a trap this family sets more than
// once. There is no frame at all: the guard is tested before anything is saved.
CUnit *__fastcall AIFindHallucinateTarget(CUnit *pUnit, int nFlags)
{
  if (nFlags)
    return NULL;
  return pUnit->sub_442BC0(0x400, ai_test_best_hallucinate_target, pUnit);
}

// AIFindIrradiateTarget.
//
// AIFindMatrixTarget's 56 bytes plus the same leading store AIFindBroodlingTarget
// has. The store is the first instruction, ahead of the frame and of the flag
// test, and it stores EDX at full dword width: the mode flag itself, not a truth
// value.
CUnit *__fastcall AIFindIrradiateTarget(CUnit *pUnit, int nFlags)
{
  int nRange;

  dword_665D30 = nFlags;
  if (nFlags)
    nRange = 0x120;
  else
    nRange = aiscript_has_campaign(pUnit->bPlayer) ? 0x800 : 0x800;

  return pUnit->sub_442BC0(nRange, ai_test_best_irradiate_target, pUnit);
}

// AIFindRepairTarget.
//
// The bare forward, the same seventeen bytes as sub_432BD0 and AIFindCureTarget
// above, through the radius search. There is no frame and no test of EDX at all;
// the parameter is declared because the one call site, ai_update_unit_spells,
// dispatches the whole family uniformly, and an unread register parameter costs
// no byte. No naming source here names it; the name comes from its filter,
// sub_432870: a unit of the same player that has come to this one and that this
// one can repair (order 0x22).
CUnit *__fastcall AIFindRepairTarget(CUnit *pUnit, int nFlags)
{
  return pUnit->sub_442BC0(0x800, sub_432870, pUnit);
}

// AIFindMagnapulseTarget.
//
// The mask form again: `and eax, 400h / add eax, 400h`, so arms of 0x800 and
// 0x400.
CUnit *__fastcall AIFindMagnapulseTarget(CUnit *pUnit, int nFlags)
{
  int nRange;

  if (nFlags)
    nRange = 0x120;
  else
    nRange = aiscript_has_campaign(pUnit->bPlayer) ? 0x800 : 0x400;

  return pUnit->sub_442BC0(nRange, ai_test_best_magnapulse_target, pUnit);
}

// AIFindMindControlTarget.
//
// The bare forward, filter 433460h at radius 0x400.
// sub_432E70.
// Address-taken by CUnit::sub_432BB0 as FindNearestInRect's filter. Four guards share one
// trailing failure epilogue, so they are one `&&` chain. The tail is the normalised 0/1 of
// the pointer comparison `town_repair_worker(pCandidate) == pCandidate`, not a truth test of
// the result. +0x134 is tested as a whole dword, through CUnit::dwUnion134.
BOOL __fastcall sub_432E70(CUnit *pCandidate, CUnit *pSource)
{
  if (pCandidate->bPlayer == pSource->bPlayer &&
      (gulUnitTypeFlags[pCandidate->wType] & 8) &&
      !(pCandidate->dwFlags & 0x40) &&
      pCandidate->dwUnion134 != 0)
    return town_repair_worker(pCandidate) == pCandidate;

  return FALSE;
}

CUnit *__fastcall AIFindMindControlTarget(CUnit *pUnit, int nFlags)
{
  return pUnit->sub_442BC0(0x400, ai_test_best_mind_control_target, pUnit);
}

// AIFindMyopiaTarget.
//
// Both arms equal again, at 0x400 rather than AIFindMatrixTarget's 0x800: no
// neg/sbb, so the discarded-call fold applies and the ternary stays written out.
CUnit *__fastcall AIFindMyopiaTarget(CUnit *pUnit, int nFlags)
{
  int nRange;

  if (nFlags)
    nRange = 0x120;
  else
    nRange = aiscript_has_campaign(pUnit->bPlayer) ? 0x400 : 0x400;

  return pUnit->sub_442BC0(nRange, ai_test_best_myopia_target, pUnit);
}

// CUnit::AIFindNearestBunker.
//
// If this player owns no completed Bunker, no search is attempted; otherwise it
// defers to the same fixed-area search CUnit::sub_431F00 uses, with another
// filter. Unlike that body it passes bPlayer straight into the call rather than
// caching it in a local, which is 1.17's own asymmetry: there is one poll here
// and nothing to hoist.
CUnit *CUnit::AIFindNearestBunker()
{
  if (count_get_completion(0x7D, bPlayer))
    return FindNearestInRect(pHarvestSearchData, ai_test_bunker_target, this);
  return NULL;
}

// AIFindNukeTarget.
//
// The bare forward through the fixed-area searcher rather than the radius one:
// nuking has no radius, so the search area is the whole map (sgMapPixRect_g) and
// the call is FindNearestInRect. Otherwise AIFindMindControlTarget's shape, with
// one register serving as both the context and the `this`.
CUnit *__fastcall AIFindNukeTarget(CUnit *pUnit)
{
  return pUnit->FindNearestInRect(pHarvestSearchData, ai_test_best_nuke_target, pUnit);
}

// AIFindNukeTargetInArea.
// Set the search rectangle, clear the fallback slot, run the fixed-area search with
// ai_test_best_nuke_target_in_area as the filter, and return its hit or the fallback the
// filter recorded. The parameters are a pointer in ECX and an 8-byte aggregate by value on
// the stack (`ret 8h`); EDX is unused. The fallback is cleared before the struct copy, and the
// tail is one ternary, loaded before the test for the cmovnz.
CUnit *__fastcall AIFindNukeTargetInArea(CUnit *pUnit, mrect_t area)
{
  CUnit *pResult;

  dword_665D34 = 0;
  stru_665D38 = area;

  pResult = pUnit->FindNearestInRect(pHarvestSearchData, ai_test_best_nuke_target_in_area, pUnit);
  return pResult ? pResult : (CUnit *)dword_665D34;
}

// AIFindParalyzeTarget.
//
// The leading dword_665D30 store plus the mask form, arms of 0x800 and 0x400.
CUnit *__fastcall AIFindParalyzeTarget(CUnit *pUnit, int nFlags)
{
  int nRange;

  dword_665D30 = nFlags;
  if (nFlags)
    nRange = 0x120;
  else
    nRange = aiscript_has_campaign(pUnit->bPlayer) ? 0x800 : 0x400;

  return pUnit->sub_442BC0(nRange, ai_test_best_paralyze_target, pUnit);
}

// AIFindParasiteCureTarget.
//
// The guarded forward through the fixed-area searcher: AIFindHallucinateTarget's
// twenty-four bytes with FindNearestInRect in place of sub_442BC0, and the same
// inverted branch sense, where the `jz` skips the `xor eax,eax / ret` pair and
// the search runs on a zero flag.
CUnit *__fastcall AIFindParasiteCureTarget(CUnit *pUnit, int nFlags)
{
  if (nFlags)
    return NULL;
  if (!HxOwnUnitUnder(pUnit, true))  // Hyperion: none to find
    return HxFindCheck() ? HxFindChecked("parasite cure", pUnit->FindNearestInRect(
                                                              pHarvestSearchData, ai_test_best_parasite_cure_target, pUnit))
                         : NULL;
  return pUnit->FindNearestInRect(pHarvestSearchData, ai_test_best_parasite_cure_target, pUnit);
}

// AIFindParasiteTarget.
//
// AIFindMatrixTarget's shape byte for byte, with filter 433870h: no leading store
// and both ternary arms equal at 0x800.
CUnit *__fastcall AIFindParasiteTarget(CUnit *pUnit, int nFlags)
{
  int nRange;

  if (nFlags)
    nRange = 0x120;
  else
    nRange = aiscript_has_campaign(pUnit->bPlayer) ? 0x800 : 0x800;

  return pUnit->sub_442BC0(nRange, ai_test_best_parasite_target, pUnit);
}

// AIFindPlagueTarget.
//
// AIFindParasiteTarget's shape with the folded constant 0x1000 instead of 0x800
// and predicate 433A50h, the same discarded-call fold.
CUnit *__fastcall AIFindPlagueTarget(CUnit *pUnit, int nFlags)
{
  int nRange;

  if (nFlags)
    nRange = 0x120;
  else
    nRange = aiscript_has_campaign(pUnit->bPlayer) ? 0x1000 : 0x1000;

  return pUnit->sub_442BC0(nRange, ai_test_best_plague_target, pUnit);
}

// Hyperion: a mod's spell whose splash spares the caster's side. Psionic
// Storm's test above without its count of the caster's own units, which a spell of a melee
// caster always fails, and with the spell's own weapon.
static DWORD s_dwModClusterWeapon;

static BOOL __fastcall ai_test_mod_cluster_target(CUnit *pCandidate, CUnit *pSource)
{
  if (!sub_433DF0(pSource, pCandidate))
    return FALSE;
  if (!pSource->CanTargetWithWeapon(static_cast<BYTE>(s_dwModClusterWeapon), pCandidate))
    return FALSE;
  CSprite *pSprite = pCandidate->pSprite;
  dword_665D28 = 0;
  dword_665D24 = s_dwModClusterWeapon;
  find_first_unit_near(pSprite->wPosX, PosU(pSprite->wPosY), 0x60, ai_query_area_enemy_hp, pSource);
  return dword_665D30 != 0 || dword_665D28 >= 0xFA;
}

CUnit *__fastcall AIFindModClusterTarget(CUnit *pUnit, int nFlags, int nWeapon, int nRange)
{
  s_dwModClusterWeapon = static_cast<DWORD>(nWeapon);
  dword_665D30 = nFlags;
  if (nRange == 0)
    nRange = nFlags ? 0x120 : aiscript_has_campaign(pUnit->bPlayer) ? 0x800 : 0x400;
  return pUnit->sub_442BC0(nRange, ai_test_mod_cluster_target, pUnit);
}

// AIFindPsiStormTarget.
//
// AIFindParalyzeTarget's shape with predicate 433B10h instead: the same leading
// dword_665D30 store and the same mask, so arms of 0x800 and 0x400.
CUnit *__fastcall AIFindPsiStormTarget(CUnit *pUnit, int nFlags)
{
  int nRange;

  dword_665D30 = nFlags;
  if (nFlags)
    nRange = 0x120;
  else
    nRange = aiscript_has_campaign(pUnit->bPlayer) ? 0x800 : 0x400;

  return pUnit->sub_442BC0(nRange, ai_test_best_psistorm_target, pUnit);
}

// AIFindPsyFeedbackTarget.
//
// The masked two-constant ternary with no leading global store, predicate
// 433BB0h; the 6-byte gap against AIFindPsiStormTarget is exactly that store.
CUnit *__fastcall AIFindPsyFeedbackTarget(CUnit *pUnit, int nFlags)
{
  int nRange;

  if (nFlags)
    nRange = 0x120;
  else
    nRange = aiscript_has_campaign(pUnit->bPlayer) ? 0x800 : 0x400;

  return pUnit->sub_442BC0(nRange, ai_test_best_feedback_target, pUnit);
}

// AIFindRecallTarget.
// The fixed-area search (sgMapPixRect_g with ai_test_best_teleport_target), gated on the
// searching unit's own region being acceptable for its player: the search runs when the gate
// is true. The false arm returns EAX unzeroed, reachable only when it is already 0, so it is
// spelled `return NULL;`. One parameter: the call site sets ECX and no EDX. bPlayer is cached
// in a BYTE local declared before the GetRegion_CUnit call.
CUnit *__fastcall AIFindRecallTarget(CUnit *pUnit)
{
  BYTE bPlayer = pUnit->bPlayer;

  if (!tai_captain_region_is_calm(bPlayer, GetRegion_CUnit(pUnit)))
    return NULL;

  return pUnit->FindNearestInRect(pHarvestSearchData, ai_test_best_teleport_target, pUnit);
}

// AIFindStasisTarget.
//
// The mask form, arms of 0x800 and 0x400, predicate 433D10h.
CUnit *__fastcall AIFindStasisTarget(CUnit *pUnit, int nFlags)
{
  int nRange;

  if (nFlags)
    nRange = 0x120;
  else
    nRange = aiscript_has_campaign(pUnit->bPlayer) ? 0x800 : 0x400;

  return pUnit->sub_442BC0(nRange, ai_test_best_stasis_target, pUnit);
}

// AIFindSwarmTarget.
// The range-selecting forward, its arms 0x800 and 0x400 joined by a `jmp` as in
// AIFindEMPTarget, because the result is post-processed. A null result is returned at once;
// a non-null one with dwFlags bit 2 set returns its pOrderTarget (+0x5C) instead of itself,
// the test byte-wide. The early null keeps its own `return`: one `if` for both tests merges
// the two `ret`s 1.17 has.
CUnit *__fastcall AIFindSwarmTarget(CUnit *pUnit, int nFlags)
{
  int nRange;
  CUnit *pTarget;

  if (nFlags)
    nRange = 0x120;
  else
    nRange = aiscript_has_campaign(pUnit->bPlayer) ? 0x800 : 0x400;

  pTarget = pUnit->sub_442BC0(nRange, ai_test_best_swarm_target, pUnit);
  if (!pTarget)
    return NULL;

  if (pTarget->dwFlags & 4)
    return pTarget->pOrderTarget;

  return pTarget;
}

// AIFindUnpoweredBldg.
// A one-line forward into the rect-clipped search sub_442D40, splitting a packed coordinate
// pair on the way. ECX is the player index, not a CUnit*: it is pushed as sub_442D40's
// trailing context and compared by ai_test_best_unpowered_target against a candidate's
// bPlayer. The packed pair is unsigned and handed on unmasked for the callee to narrow.
CUnit *__fastcall AIFindUnpoweredBldg(int nPlayer, mpoint_t pos)
{
  // Hyperion: 1.17 passes the point as one dword and its high half
  return sub_442D40(pos.x, pos.y,
                    pHarvestSearchData, ai_test_best_unpowered_target, nPlayer);
}

// AIFindYamatoTarget.
// Two different searchers behind one filter: the nonzero arm takes the radius search (0x200)
// and the zero arm the fixed-area one (sgMapPixRect_g), so this is not a range ternary. The
// source is two plain returns; MSVC hoists the shared pushes above the branch.
CUnit *__fastcall AIFindYamatoTarget(CUnit *pUnit, int nFlags)
{
  if (nFlags)
    return pUnit->sub_442BC0(0x200, ai_test_best_yamato_target, pUnit);
  return pUnit->FindNearestInRect(pHarvestSearchData, ai_test_best_yamato_target, pUnit);
}

// ai_test_best_hallucinate_target.
// The player guard loads pSource's byte first, where sub_432870 loads the candidate's, which
// is the only difference between the two prologues. The type ladder is
// `if (t != A && t != B && t != C && t != D) return FALSE;`; the positive form reorders it.
// The sprite position is sub_432870's movzx-then-movsx form. 0xE4 (228) is the "no type"
// sentinel, one past the last units.dat row.
BOOL __fastcall ai_test_best_hallucinate_target(CUnit *pCandidate, CUnit *pSource)
{
  CSprite *pSprite;
  WORD wY, wX, wType;

  if (pSource->bPlayer != pCandidate->bPlayer)
    return FALSE;

  pSprite = pCandidate->pSprite;
  wY = PosU(pSprite->wPosY);
  wX = pSprite->wPosX;

  if (!pSource->sub_434130(0x14, pCandidate, 0xE4, PosS(wX), PosS(wY), NULL))
    return FALSE;

  wType = pCandidate->wType;
  if (wType != 0x48 && wType != 0x46 && wType != 0x47 && wType != 0x44)
    return FALSE;

  return TRUE;
}

// ai_test_best_heal_target.
// A search predicate CUnit::FindHealTarget registers: two plain `if (...) return FALSE;`
// statements, not an `&&` chain. The two CUnit* are swapped for the call: the source unit
// becomes CUnit::sub_437060's `this` and the candidate its argument. +0x107 is not a named
// field: it falls inside `_unmapped_106[0x0F]` and is spelled through that array as a BYTE.
BOOL __fastcall ai_test_best_heal_target(CUnit *pCandidate, CUnit *pSource)
{
  if (pCandidate->_unmapped_106[1])
    return FALSE;

  if (pCandidate->dwFlags & 0x40000000)
    return FALSE;

  return pSource->sub_437060(pCandidate);
}

// ai_test_best_irradiate_target.
// "Is this a good irradiate victim." Five guards share one early failure epilogue; the tail
// is one `if` whose body is a negated `&&` chain,
// `if (a == 0 && b()) { if (!(f & 8) && t != 0x2A && t != 0x22) return FALSE; } return TRUE;`.
// wType is read once into a 32-bit local. +0x118 on the candidate is bIrradiateTimer, a byte
// compared against zero.
BOOL __fastcall ai_test_best_irradiate_target(CUnit *pCandidate, CUnit *pSource)
{
  if (!sub_433DF0(pSource, pCandidate))
    return FALSE;

  if (pCandidate->bIrradiateTimer != 0)
    return FALSE;

  if (!pSource->CanTargetWithWeapon(0x22, pCandidate))
    return FALSE;

  if (!pCandidate->IsIrradiateTargetForAI())
    return FALSE;

  if (dword_665D30 == 0 && aiscript_has_campaign(pSource->bPlayer))
  {
    if (!(gulUnitTypeFlags[pCandidate->wType] & 8) &&
        pCandidate->wType != 0x2A && pCandidate->wType != 0x22)
      return FALSE;
  }
  return TRUE;
}

// ai_count_collateral_energy.
// An `&&` chain whose four conditions guard one statement, the failure path shared with the
// final return. It always returns FALSE, so the dispatching search visits every candidate.
// The byte at +0xA3 is the high byte of wEnergy at +0xA2, `(BYTE)(wEnergy >> 8)`: whole
// energy points on the x256 fixed point. The flag and units.dat tests are dword-wide, their
// masks needing bits above 16.
BOOL __fastcall ai_count_collateral_energy(CUnit *pCandidate, CUnit *pSource)
{
  if (!(pCandidate->dwFlags & 0x4000000)
      && pSource->IsEnemyUnit(pCandidate)
      && (gulUnitTypeFlags[pCandidate->wType] & 0x200000)
      && !(pCandidate->dwFlags & 0x40000000))
  {
    dword_665D28 += (BYTE)(pCandidate->wEnergy >> 8);
  }

  return FALSE;
}

// The plain predicates.

// oracle 0x433360 (162 B). Address-taken only; src117/h/CUnitSearches.h declares
// it. Separate `if (...) return FALSE;` guards rather than an `&&` chain: the
// failure epilogue sits before every guard and all six jump backward into it. EDI
// is pushed late because the widened unit type it holds is not live until after
// those guards.
//
// The order-target arm wants !IsEnemyUnit -- `neg / sbb / inc`, the inverse of
// the spine's `neg / sbb / neg`, with the following `jnz` going to the true
// epilogue -- so a candidate qualifies when its order target is a non-enemy of
// the source. That normalised boolean is computed and only then branched on,
// which a plain `if (.. && !pSource->IsEnemyUnit(pT))` never emits: MSVC branches
// on the raw result and the body comes three bytes short. The same block appears
// four times in this TU, here, in ai_test_best_swarm_target and twice in
// ai_test_best_disruptor_target, so it is one source construct rather than a
// repeated hand-written idiom.
static BOOL __fastcall ai_order_target_not_enemy(CUnit *pUnit, CUnit *pSource)
{
  CUnit *pTarget = pUnit->pOrderTarget;

  if (pTarget == NULL)
    return FALSE;

  if (!PlayerIsLobby(pTarget->bPlayer))  // Hyperion: >= 8 in 1.17
    return FALSE;

  return !pSource->IsEnemyUnit(pTarget);
}

BOOL __fastcall ai_test_best_magnapulse_target(CUnit *pCandidate, CUnit *pSource)
{
  int nType;

  if (!sub_433DF0(pSource, pCandidate))
    return FALSE;

  if (!pSource->CanTargetWithWeapon(0x20, pCandidate))
    return FALSE;

  if (pCandidate->dwFlags & 0x400)
    return FALSE;

  if (pCandidate->bLockdownTimer != 0)
    return FALSE;

  if (pCandidate->bStasisTimer != 0)
    return FALSE;

  if (pCandidate->bParalyzeTimer != 0)
    return FALSE;

  nType = pCandidate->wType;

  if ((nType == 0x45 || nType == 0x0B) &&
      (pCandidate->bMoveFlags & 2) &&
      pCandidate->IsUnitInside())
    return TRUE;

  if (nType == 0x48 || nType == 0x53)
    return TRUE;

  if (ai_order_target_not_enemy(pCandidate, pSource))
    return TRUE;

  return FALSE;
}

// ai_test_best_myopia_target.
// A true predicate, its failure epilogue at the end: one `&&` chain guarding a block, with a
// single trailing `return FALSE;`; three `if (...) return FALSE;` statements put the epilogue
// first. The opening register shuffle is an argument swap: sub_433DF0 takes the source in
// ECX and the candidate in EDX. The toughness compare is signed, GetToughness being `int`.
BOOL __fastcall ai_test_best_myopia_target(CUnit *pCandidate, CUnit *pSource)
{
  if (sub_433DF0(pSource, pCandidate)
      && !(gulUnitTypeFlags[pCandidate->wType] & 1)
      && !UnitMyopia(pCandidate).Any())  // Hyperion: bUnion123, and the blinders past 7 (stage D)
  {
    if (pCandidate->sub_431A90())
      return TRUE;

    return pCandidate->GetToughness() > 0x50;
  }

  return FALSE;
}

// oracle 0x4335B0 (106 B). ai_test_best_nuke_target, the collateral sweep's own
// reader: it zeroes dword_665D28, runs the sweep and compares the total against
// 0x320.
BOOL __fastcall ai_test_best_nuke_target(CUnit *pCandidate, CUnit *pSource)
{
  CSprite * __restrict pSprite;

  if ((pCandidate->dwFlags & 0x300) &&
      !UnitDetectedBy(pCandidate, pSource->bPlayer))  // Hyperion: every slot (stage D)
    return FALSE;

  if (!pSource->IsEnemyUnit(pCandidate))
    return FALSE;

  pSprite = pCandidate->pSprite;
  {
    upos_t y = PosU(pSprite->wPosY);
    upos_t x = pSprite->wPosX;
    dword_665D28 = 0;
    find_first_unit_near(x, y, 0xC0, ai_query_collateral_damage, pSource);
  }
  return dword_665D28 >= 0x320;
}

// -- a displaced literal in the shipped form; see the block above --

// oracle 0x433620 (129 B). The one body in the family that reads the coordinates
// twice, and the first read is bare 16-bit, compared against stru_665D38's four
// members with signed 16-bit compares: the pair is signed, the members `short`.
//
// The position has to be a point16_t rather than two `short` locals: a
// materialised 16-bit local widens with `movzx`, where MSVC keeps a struct
// member's register at the member's own width, 1.17's bare 16-bit move. The two
// levers do not swap places -- `__restrict` does nothing here, there being no
// store to schedule around, and the aggregate does nothing at the survey call,
// where a struct member fed to a fastcall slot forces a frame pointer and spills.
// The survey re-reads the sprite and both coordinates rather than reusing those
// locals, because CUnit::IsEnemyUnit clobbers the registers holding them. All
// five rejects jump forward: one `&&` chain.
BOOL __fastcall ai_test_best_nuke_target_in_area(CUnit *pCandidate, CUnit *pSource)
{
  CSprite * __restrict pSprite = pCandidate->pSprite;
  mpoint_t  pt;

  pt.x = PosS(pSprite->wPosX);
  pt.y = pSprite->wPosY;

  if (pt.x >= stru_665D38.left && pt.x <= stru_665D38.right &&
      pt.y >= stru_665D38.top && pt.y <= stru_665D38.bottom &&
      pSource->IsEnemyUnit(pCandidate))
  {
    CSprite * __restrict pSprite2 = pCandidate->pSprite;
    upos_t sy = PosU(pSprite2->wPosY);
    upos_t sx = pSprite2->wPosX;

    dword_665D34 = reinterpret_cast<DWORD_PTR>(pCandidate);
    dword_665D28 = 0;
    find_first_unit_near(sx, sy, 0xC0, ai_query_collateral_damage, pSource);

    return dword_665D28 >= 0x320;
  }

  return FALSE;
}

// oracle 0x433840 (46 B). No frame at all: ECX carries pCandidate straight into
// CanTargetWithWeapon's pushed argument slot. The failure epilogue sits first and
// the +0x124 guard jumps backward into it, the separate-`return FALSE;` shape
// rather than an `&&` chain. The `neg eax / sbb eax,eax / neg eax` tail is
// `!= FALSE` on the call result.
BOOL __fastcall ai_test_best_parasite_cure_target(CUnit *pCandidate, CUnit *pSource)
{
  if (pCandidate->bPlayer != pSource->bPlayer)
    return FALSE;

  if (!UnitParasite(pCandidate).Any() && !pCandidate->bParalyzeTimer)  // Hyperion: every slot
    return FALSE;

  return pSource->CanTargetWithWeapon(0x66, pCandidate) != FALSE;
}

// oracle 0x433870 (156 B). Address-taken only; src117/h/CUnitSearches.h declares
// it.
//
// +0x121 is read as a per-player bit mask, twice over: `cmp byte ptr [esi+121h],
// 0h` for any player, then `mov eax, 1h / shl eax, cl / test byte ptr [esi+121h],
// al` with CL the source unit's bPlayer -- one bit per player, the shape CSprite
// +0x0C carries. CUnit.h still spells it `_unmapped_121[0x02]`.
//
// The first aiscript_has_campaign call reads the field and the second reads a
// cached byte, so there is a BYTE local between the two rather than a re-read.
// The final `neg / sbb / neg` is `!= FALSE`, against the magnapulse arm's
// `neg / sbb / inc`, which is `== FALSE`.
BOOL __fastcall ai_test_best_parasite_target(CUnit *pCandidate, CUnit *pSource)
{
  BYTE bPlayer;

  if (!sub_433DF0(pSource, pCandidate))
    return FALSE;

  if (aiscript_has_campaign(pSource->bPlayer) && UnitParasite(pCandidate).Any())  // Hyperion
    return FALSE;

  if (UnitParasite(pCandidate).Has(pSource->bPlayer))  // Hyperion: every slot
    return FALSE;

  bPlayer = pSource->bPlayer;

  if (!aiscript_has_campaign(bPlayer) &&
      !pCandidate->sub_431A90() &&
      !pCandidate->IsATransport() &&
      !pCandidate->HasEnergy() &&
      !(gulUnitTypeFlags[pCandidate->wType] & 8) &&
      pCandidate->GetToughness() < 0x12C)
    return FALSE;

  return pSource->CanTargetWithWeapon(0x38, pCandidate) != FALSE;
}

// -- a displaced literal in the shipped form; see the block above --

// oracle 0x433A50 (192 B). Two surveys: the first accumulates enemy hit points
// into dword_665D28, the second ally hit points into dword_665D2C, and the
// verdict is `2 * ally < enemy` -- `add eax,eax / cmp eax,[665D28] /
// sbb eax,eax / neg eax` is CF, the unsigned borrow. The four entry guards and
// the mid-body reject all jump forward to the same trailing `return FALSE;`.
BOOL __fastcall ai_test_best_plague_target(CUnit *pCandidate, CUnit *pSource)
{
  if (sub_433DF0(pSource, pCandidate) &&
      pSource->CanTargetWithWeapon(0x3C, pCandidate) &&
      pCandidate->bBloodBoilTimer == 0 &&
      !(gulUnitTypeFlags[pCandidate->wType] & 0x40))
  {
    {
      CSprite * __restrict pSprite = pCandidate->pSprite;
      upos_t y = PosU(pSprite->wPosY);
      upos_t x = pSprite->wPosX;

      dword_665D28 = 0;
      dword_665D24 = 0x3C;
      find_first_unit_near(x, y, 0x60, ai_query_area_enemy_hp, pSource);
    }

    if (dword_665D30 == 0 && dword_665D28 < 0xFA)
      return FALSE;

    {
      CSprite * __restrict pSprite = pCandidate->pSprite;
      upos_t y = PosU(pSprite->wPosY);
      upos_t x = pSprite->wPosX;

      dword_665D2C = 0;
      find_first_unit_near(x, y, 0x60, ai_query_area_ally_hp, pSource);
    }

    return dword_665D2C * 2 < dword_665D28;
  }

  return FALSE;
}

// oracle 0x433B10 (157 B). The plague body's shape, with the two entry guards
// sharing an epilogue emitted after the first one and jumped backward into by the
// second, and with no unit-type or timer test.
BOOL __fastcall ai_test_best_psistorm_target(CUnit *pCandidate, CUnit *pSource)
{
  if (!sub_433DF0(pSource, pCandidate))
    return FALSE;

  if (!pSource->CanTargetWithWeapon(0x54, pCandidate))
    return FALSE;

  {
    CSprite * __restrict pSprite = pCandidate->pSprite;
    upos_t y = PosU(pSprite->wPosY);
    upos_t x = pSprite->wPosX;

    dword_665D28 = 0;
    dword_665D24 = 0x54;
    find_first_unit_near(x, y, 0x60, ai_query_area_enemy_hp, pSource);
  }

  if (dword_665D30 == 0 && dword_665D28 < 0xFA)
    return FALSE;

  {
    CSprite * __restrict pSprite = pCandidate->pSprite;
    upos_t y = PosU(pSprite->wPosY);
    upos_t x = pSprite->wPosX;

    dword_665D2C = 0;
    find_first_unit_near(x, y, 0x60, ai_query_area_ally_hp, pSource);
  }

  return dword_665D2C * 2 < dword_665D28;
}

// ai_test_best_feedback_target.
// The failure epilogue sits at the end, every test jumping forward to it: the `&&` chain
// below, not a sequence of `if (...) return FALSE;`. One load per object, two tests each,
// kept in the locals: gulUnitTypeFlags[wType] for 0x200000 and 0x40, CUnit::dwFlags for
// 0x40000000 and 2. In the tail CUnit::GetToughness runs before the energy load, and the
// compare is a signed >= in whole energy points, the high byte of wEnergy.

BOOL __fastcall ai_test_best_feedback_target(CUnit *pCandidate, CUnit *pSource)
{
  DWORD ulTypeFlags;
  DWORD ulUnitFlags;

  if (sub_433DF0(pSource, pCandidate))
  {
    ulTypeFlags = gulUnitTypeFlags[pCandidate->wType];
    if (ulTypeFlags & 0x200000)
    {
      ulUnitFlags = pCandidate->dwFlags;
      if (!(ulUnitFlags & 0x40000000) && !(ulUnitFlags & 0x02)
          && !(ulTypeFlags & 0x40))
        return (int)(pCandidate->wEnergy >> 8) >= pCandidate->GetToughness();
    }
  }

  return FALSE;
}

// ai_test_best_teleport_target.
// The ai_test_best_* family's recurring shape in its smallest form: a sequence of
// `if (...) return FALSE;` guards, the failure epilogue early; the type ladder as
// `if (t != A && t != B && t != C && t != D) return FALSE;`; and a signed compare, a cast at
// the use since CUnit::lShields is DWORD. `& ~0xFF` drops the x256 fraction, so this compares
// whole shield points (0xA00 is 10).
BOOL __fastcall ai_test_best_teleport_target(CUnit *pCandidate, CUnit *pSource)
{
  WORD wType;

  if (pCandidate->bPlayer != pSource->bPlayer)
    return FALSE;

  if (!pCandidate->pOrderTarget)
    return FALSE;

  wType = pCandidate->wType;
  if (wType != 0x48 && wType != 0x52 && wType != 0x53 && wType != 0x51)
    return FALSE;

  return ((int)pCandidate->lShields & ~0xFF) <= 0xA00;
}

// CUnit::AISetMinRange.
// Cache the smaller of this unit's ground and air weapon minimum ranges into dword_665D20,
// for the attack-target search. -0x7E (130) is weapons.dat's "no weapon" sentinel, compared
// signed. Four exits, one per combination: both weapons take the min, an unsigned compare so
// the ternary's operands are DWORD; either alone stores its own range; neither stores 0. The
// (BYTE) casts on the index make it zero-extend, the weapon ids being `char`.
void CUnit::AISetMinRange()
{
  weapon_id_t bGroundWeapon = GetWeaponGnd();
  weapon_id_t bAirWeapon = GetWeaponAir();

  if (bGroundWeapon != WEAPON_NONE) {
    if (bAirWeapon != WEAPON_NONE) {
      DWORD dwGroundRange = wpn_minrange_g[bGroundWeapon];
      DWORD dwAirRange = wpn_minrange_g[bAirWeapon];
      dword_665D20 = dwGroundRange < dwAirRange ? dwGroundRange : dwAirRange;
    }
    else {
      dword_665D20 = wpn_minrange_g[bGroundWeapon];
    }
  }
  else if (bAirWeapon != WEAPON_NONE) {
    dword_665D20 = wpn_minrange_g[bAirWeapon];
  }
  else {
    dword_665D20 = 0;
  }
}

// ai_count_collateral_shields.
// ai_count_collateral_energy's twin: the same `&&` chain into the same accumulator and the
// same unconditional FALSE, with a units.dat BYTE table for the eligibility test and one
// fewer flag guard. lShields is DWORD, so the cast to signed sits at the use for the
// arithmetic shift: whole shield points, the fraction discarded.
BOOL __fastcall ai_count_collateral_shields(CUnit *pCandidate, CUnit *pSource)
{
  if (!(pCandidate->dwFlags & 0x4000000)
      && pSource->IsEnemyUnit(pCandidate)
      && gbUnitHasShields[pCandidate->wType])
  {
    dword_665D28 += (int)pCandidate->lShields >> 8;
  }

  return FALSE;
}

// -- a displaced literal in the shipped form; see the block above --

// oracle 0x433D10 (219 B). The plague body with two extra guards on the
// candidate's order target: it must exist, its bPlayer must be below 8 (an
// unsigned compare), and it must not be an enemy of pSource.
//
// That last guard is a ternary, not a third `&&`, and three bytes say so: 1.17
// does not branch on IsEnemyUnit's result but normalises it first, `neg eax /
// sbb eax,eax / inc eax`, then reads the ZF the `inc` set. Spelled
// `&& !pSource->IsEnemyUnit(pTarget)` the body is 216 B with a plain
// `test eax,eax / jne` -- correct, three bytes short, every other byte identical.
// `c ? !f(x) : FALSE` is the two-predecessor diamond that makes MSVC materialise
// the value, and it is equivalent to the `&&`, short circuit included: when
// bPlayer is 8 or more the value is FALSE and f is not called. Other spellings of
// the same test were measured and fold back to `test/jne`.
BOOL __fastcall ai_test_best_stasis_target(CUnit *pCandidate, CUnit *pSource)
{
  CUnit *pTarget;

  if (sub_433DF0(pSource, pCandidate) &&
      pSource->CanTargetWithWeapon(0x53, pCandidate) &&
      !(gulUnitTypeFlags[pCandidate->wType] & 1) &&
      (pTarget = pCandidate->pOrderTarget) != NULL &&
      (PlayerIsLobby(pTarget->bPlayer) ? !pSource->IsEnemyUnit(pTarget) : FALSE))  // Hyperion
  {
    {
      CSprite * __restrict pSprite = pCandidate->pSprite;
      upos_t y = PosU(pSprite->wPosY);
      upos_t x = pSprite->wPosX;

      dword_665D28 = 0;
      dword_665D24 = 0x53;
      find_first_unit_near(x, y, 0x60, ai_query_area_enemy_hp, pSource);
    }

    if (dword_665D30 == 0 && dword_665D28 < 0xFA)
      return FALSE;

    {
      CSprite * __restrict pSprite = pCandidate->pSprite;
      upos_t y = PosU(pSprite->wPosY);
      upos_t x = pSprite->wPosX;

      dword_665D2C = 0;
      find_first_unit_near(x, y, 0x60, ai_query_area_ally_hp, pSource);
    }

    return dword_665D2C * 2 < dword_665D28;
  }

  return FALSE;
}

// sub_433DF0.
// The shared "can this source unit legally target this candidate at all" test that
// ai_test_best_irradiate_target and ai_test_best_myopia_target open with. Its argument order
// is the reverse of the family's: the first supplies only bPlayer and is CUnit::IsEnemyUnit's
// `this`, the second is the unit read. The flag guard is a short-circuit `&&`, and both type
// ladders are plain positive `||` chains over 32-bit locals.
BOOL __fastcall sub_433DF0(CUnit *pSource, CUnit *pCandidate)
{
  int nPlayer;
  int nType;
  int nOrder;

  nPlayer = pSource->bPlayer;

  if (!SpriteSeenBy(pCandidate->pSprite, nPlayer))  // Hyperion: and slots past 7
    return FALSE;

  if ((pCandidate->dwFlags & 0x300) &&
      !UnitDetectedBy(pCandidate, nPlayer))  // Hyperion: every slot (stage D)
    return FALSE;

  if (!pSource->IsEnemyUnit(pCandidate))
    return FALSE;

  nType = pCandidate->wType;
  if (nType == 0x49 || nType == 0x0D || nType == 0x55)
    return FALSE;

  nOrder = pCandidate->bOrderId;
  if (nOrder == 0x51 || nOrder == 0x52 || nOrder == 0x53)
    return FALSE;

  return TRUE;
}

// ai_test_best_unpowered_target.
// Address-taken only, by AIFindUnpoweredBldg, as sub_442D40's filter. EDX is an int player
// index, not a CUnit* (src117/h/CUnitSearches.h); the candidate's bPlayer is zero-extended to
// meet it. One `&&` chain ending in `return TRUE;`; two `if (...) return FALSE;` statements
// fold the true arm into a setcc.
BOOL __fastcall ai_test_best_unpowered_target(CUnit *pCandidate, int nPlayer)
{
  if ((pCandidate->dwFlags & 0x400) && pCandidate->bPlayer == nPlayer)
    return TRUE;

  return FALSE;
}

// Accepts a Yamato target: an attacker, a loaded bunker, or a unit of moderate toughness
// whose type lacks flag 0x40. Not type 0x24.
// Each rejection returns on its own; an if/return on the inverted char keeps the oracle's
// `movsx eax, al` that every single-expression return folds away.
BOOL __fastcall ai_test_best_yamato_target(CUnit *pCandidate, CUnit *pSource)
{
  int nTough, nType;
  char cFlags;

  if (!sub_433DF0(pSource, pCandidate))
    return FALSE;
  if ((pCandidate->dwFlags & 2) && pCandidate->CanAttack())
    return TRUE;
  nType = pCandidate->wType;
  if (nType == 0x7D && pCandidate->IsUnitInside())
    return TRUE;
  if (nType == 0x24)
    return FALSE;
  nTough = pCandidate->GetToughness();
  if (nTough < 200 || nTough > 450)
    return FALSE;
  cFlags = static_cast<char>(~gulUnitTypeFlags[pCandidate->wType]);
  if (cFlags & 0x40)
    return TRUE;
  return FALSE;
}

// Spider-mine targeting.

// gubUnitMoveClass -- BYTE[228] @0x520B80, spelled as in CUnitZBuild.cpp.
#ifdef OC_DLL_MODE
  #ifndef gubUnitMoveClass
  #define gubUnitMoveClass (reinterpret_cast<BYTE *>(OC_VA(0x520B80u)))
  #endif
#else
extern BYTE gubUnitMoveClass[228] OC_UNALIGNED;
#endif

// Accepts an enemy that is not invincible, not flagged 0x6 and not in
// movement class 0xC1.
BOOL __fastcall SearchMineTarget(CUnit *pCandidate, CUnit *pSource)
{
  if (pSource->IsEnemyUnit(pCandidate) &&
      !(pCandidate->dwFlags & 0x4000000) &&
      !(pCandidate->dwFlags & 6) &&
      gubUnitMoveClass[pCandidate->wType] != 0xC1)
    return TRUE;
  return FALSE;
}

CUnit *CUnit::FindMineTarget()
{
  if (dwFlags & 0x8000)
    return NULL;
  return sub_442BC0((WORD)(EnemyTrackingRange() << 5), SearchMineTarget, this);
}

// Adds the toughness of each targetable enemy with type flag 0x10000 and no
// paralysis into dword_665D28; always FALSE, so the sweep visits every one.
BOOL __fastcall sub_4321E0(CUnit *pCandidate, CUnit *pSource)
{
  if (pCandidate != pSource &&
      !(pCandidate->dwFlags & 0x4000000) &&
      pSource->CanTargetWithWeapon(static_cast<BYTE>(dword_665D24), pCandidate) &&
      pSource->IsEnemyUnit(pCandidate) &&
      (gulUnitTypeFlags[pCandidate->wType] & 0x10000) &&
      pCandidate->bParalyzeTimer == 0)
    dword_665D28 += pCandidate->GetToughness();
  return FALSE;
}

// The six-bucket target survey: sub_432280 files candidates into
// dword_665B70[bucket] and counts them in sgnTargets[bucket].

// dword_665B70 -- CUnit *[6][16] @0x665B70, sheet name; sgnTargets (@0x665CF0,
// BW Beta name) brackets it at 0x180 bytes, and the reader indexes rows by
// `shl edi, 6h`.
#ifndef dword_665B70
#ifdef OC_DLL_MODE
#define dword_665B70 (reinterpret_cast<CUnit *(*)[16]>(OC_VA(0x665B70u)))
#else
extern CUnit *dword_665B70[6][16];
#endif
#endif

// sgnTargets -- DWORD[6] @0x665CF0; dword_665D08 is the next name.
#ifndef sgnTargets
#ifdef OC_DLL_MODE
#define sgnTargets (reinterpret_cast<DWORD *>(OC_VA(0x665CF0u)))
#else
extern DWORD sgnTargets[6];
#endif
#endif

// dword_665D1C -- DWORD @0x665D1C, the survey's search range.
#ifndef dword_665D1C
#if defined(OC_DLL_MODE) || defined(OC_PAYLOAD)
#define dword_665D1C (*reinterpret_cast<DWORD *>(OC_VA(0x665D1Cu)))
#else
static DWORD dword_665D1C;
#endif
#endif

// Surveys the area and picks a random unit from the first non-empty bucket.
CUnit *CUnit::sub_431B70()
{
  memset(sgnTargets, 0, 6 * sizeof(DWORD));
  {
    // The range stays in a register and pThis is __restrict, so the payload, where
    // dword_665D1C is a literal address, neither reloads it nor tests the flag late.
    CUnit *__restrict pThis = this;
    DWORD dwRange = EnemyTrackingRange() << 5;

    dword_665D1C = dwRange;
    if (pThis->dwFlags & 0x20) {
      dwRange += 0x40;
      dword_665D1C = dwRange;
    }
    CSprite * __restrict pSpr = pThis->pSprite;
    upos_t y = PosU(pSpr->wPosY);
    upos_t x = pSpr->wPosX;

    dword_665D20 = 0;
    find_first_unit_near(x, y, dwRange + 0x40, sub_432280, pThis);
  }
  for (unsigned i = 0; i < 6; ++i) {
    if (sgnTargets[i] != 0) {
      CUnit **pBucket = dword_665B70[i];
      if (sgnTargets[i] == 1)
        return pBucket[0];
      return pBucket[net_rand(0x13) % sgnTargets[i]];
    }
  }
  return NULL;
}

// Scores a target for retargeting, lower is more urgent; a bunker scores its
// first passenger, and four types always score 5.
int CUnit::sub_4319D0(CUnit *pTarget)
{
  CUnit *pUnit = pTarget;
  WORD wType = pTarget->wType;
  if (wType == 0x7D) {
    CUnit *pInside = pTarget->GetFirstTransportUnit();
    if (pInside != NULL)
      pUnit = pInside;
  } else if (wType == 0x23 || wType == 0x24 || wType == 0x3B || wType == 0x61) {
    return 5;
  }

  int nScore;
  if (gulUnitTypeFlags[pUnit->wType] & 8)
    nScore = 2;
  else if (pUnit->CanTarget(this, 1))
    nScore = 0;
  else if (pUnit->CanAttack())
    nScore = 2;
  else
    nScore = (pUnit->dwFlags & 0x20000) ? 3 : 4;

  if (!(pUnit->dwFlags & 1) || pUnit != pTarget)
    ++nScore;
  if (nScore == 0 && (pUnit->dwFlags & 0x8000))
    nScore = 1;
  return nScore;
}

// Recomputes the player mask that can see this unit: 0xFF when revealed,
// otherwise nearby detectors, scanners, the owner, shared vision and the
// players flagged in +0x121. Hyperion: 1.17's games only; a game with
// Hyperion's slots works it out as a set (DetectionHx).
DWORD CUnit::sub_4316B0()
{
  if (IsRevealedBySpell() || HasFlag())
    return 0xFF;

  vision_ctx_t ctx;
  ctx.dwMask = 0;
  ctx.pSource = this;
  find_first_unit_near(pSprite->wPosX, pSprite->wPosY, 0x160,
             reinterpret_cast<CUnitSearchPredicate>(sub_4318F0),
             reinterpret_cast<CUnit *>(&ctx));
  for (CUnit *pScan = GetScannerList(); pScan != NULL; pScan = pScan->pNext) {
    if (sub_4318F0(pScan, &ctx))
      break;
  }

  BYTE bPlayers = _unmapped_121[0];
  ctx.dwMask |= (1 << bPlayer) | SharedVision[bPlayer] | bPlayers;
  DWORD dwBit = 1;
  for (int i = 0; i < 12; ++i) {
    if (bPlayers & dwBit)
      ctx.dwMask |= SharedVision[i];
    dwBit <<= 1;
  }
  return ctx.dwMask;
}

// Hyperion: CUnit::sub_4316B0 and its predicate sub_4318F0 over a set of every slot,
// for a game with Hyperion's slots (stage D). Every slot's units when the unit is revealed,
// as 1.17's 0xFF is every lobby slot's.
namespace {
struct DetectCtxHx {
  PlayerSet q;
  CUnit *pSource;
};

BOOL __fastcall DetectHxCandidate(CUnit *pCandidate, DetectCtxHx *pCtx)
{
  CUnit *pSource = pCtx->pSource;
  if (!(pCandidate->dwFlags & 0x40000000) && pCandidate->sub_431A90() &&
      pCandidate != pSource && SpriteSeenBy(pSource->pSprite, pCandidate->bPlayer)) {
    int nRange = (pCandidate->dwFlags & 2) ? 0xE0 : pCandidate->sub_423150(0) << 5;
    if (pCandidate->IsWithinTargetRange(nRange, pSource))
      pCtx->q |= PlayerBit(pCandidate->bPlayer) | PlayerVision(pCandidate->bPlayer) |
                 UnitParasite(pCandidate);
  }
  return FALSE;
}

PlayerSet DetectionHx(CUnit *pUnit)
{
  if (pUnit->IsRevealedBySpell() || pUnit->HasFlag())
    return PlayerSet::All();
  DetectCtxHx ctx = {PlayerSet::None(), pUnit};
  find_first_unit_near(pUnit->pSprite->wPosX, pUnit->pSprite->wPosY, 0x160,
             reinterpret_cast<CUnitSearchPredicate>(DetectHxCandidate),
             reinterpret_cast<CUnit *>(&ctx));
  for (CUnit *pScan = CUnit::GetScannerList(); pScan != NULL; pScan = pScan->pNext) {
    if (DetectHxCandidate(pScan, &ctx))
      break;
  }
  const PlayerSet qParasite = UnitParasite(pUnit);
  ctx.q |= PlayerBit(pUnit->bPlayer) | PlayerVision(pUnit->bPlayer) | qParasite;
  if (qParasite.Any())  // almost always empty: then the walk adds nothing
    for (int i = 0; i < PlayerSlots(); ++i)
      if (qParasite.Has(i))
        ctx.q |= PlayerVision(i);
  return ctx.q;
}
}  // namespace

// CUnit::sub_4325A0's steps over the sets: a unit not yet worked out reports every slot
// as changed, where 1.17's reports slots 0 to 7.
void DetectionUpdateHx(CUnit *pUnit)
{
  const PlayerSet qNew = DetectionHx(pUnit);
  PlayerSet qOld = UnitDetection(pUnit);
  if (qOld == qNew)
    return;
  if (pUnit->dwUnionE4 == 0x80000000)
    qOld = ~qNew;
  const PlayerSet qChanged = qOld ^ qNew;
  UnitDetectionSet(pUnit, qNew);
  const PlayerSet qRemoved = ~qNew & qChanged;
  if (qRemoved.Any())
    pUnit->RemoveTargetReferencesForPlayers(qRemoved);
  if (!(qChanged.Low32() & gdwLocalMask))
    return;
  if (pUnit->dwUnionE4 & gdwLocalMask) {
    pUnit->pSprite->Show();
    if (pUnit->subunit)
      pUnit->subunit->pSprite->Show();
  } else {
    pUnit->sub_432560();
  }
}

// ai_order_target_not_enemy forced inline; ensnare and paralyze expand it in place,
// and calling the out-of-line copy from them un-inlines it from magnapulse.
static __forceinline BOOL ai_order_target_not_enemy_inl(CUnit *pUnit, CUnit *pSource)
{
  CUnit *pTarget = pUnit->pOrderTarget;

  if (pTarget == NULL)
    return FALSE;

  if (!PlayerIsLobby(pTarget->bPlayer))  // Hyperion: >= 8 in 1.17
    return FALSE;

  return !pSource->IsEnemyUnit(pTarget);
}

// oracle 0x432AC0 (230 B). The plague body's survey pair behind a guard on an
// ensnare-free moving candidate that is chasing a friendly human's unit.
BOOL __fastcall ai_test_best_ensnare_target(CUnit *pCandidate, CUnit *pSource)
{
  if (sub_433DF0(pSource, pCandidate) &&
      pSource->CanTargetWithWeapon(0x3A, pCandidate) &&
      pCandidate->bEnsnareTimer == 0 &&
      (pCandidate->dwFlags & 0x20000) &&
      ai_order_target_not_enemy_inl(pCandidate, pSource))
  {
    {
      CSprite * __restrict pSprite = pCandidate->pSprite;
      upos_t y = PosU(pSprite->wPosY);
      upos_t x = pSprite->wPosX;

      dword_665D28 = 0;
      dword_665D24 = 0x3A;
      find_first_unit_near(x, y, 0x60, ai_query_area_enemy_hp, pSource);
    }

    if (dword_665D30 == 0 && dword_665D28 < 0xFA)
      return FALSE;

    {
      CSprite * __restrict pSprite = pCandidate->pSprite;
      upos_t y = PosU(pSprite->wPosY);
      upos_t x = pSprite->wPosX;

      dword_665D2C = 0;
      find_first_unit_near(x, y, 0x60, ai_query_area_ally_hp, pSource);
    }

    return dword_665D2C * 2 < dword_665D28;
  }

  return FALSE;
}

// oracle 0x4336B0 (313 B). Accepts a loaded overlord-type carrier outright,
// then the ensnare guard and survey pair with sub_4321E0 as the enemy filter.
BOOL __fastcall ai_test_best_paralyze_target(CUnit *pCandidate, CUnit *pSource)
{
  if (sub_433DF0(pSource, pCandidate) &&
      pSource->CanTargetWithWeapon(0x6C, pCandidate) &&
      (gulUnitTypeFlags[pCandidate->wType] & 0x10000) &&
      pCandidate->bParalyzeTimer == 0)
  {
    if (pCandidate->wType == 0x2A && (pCandidate->bMoveFlags & 2) &&
        pCandidate->IsUnitInside())
      return TRUE;

    if (!(gulUnitTypeFlags[pCandidate->wType] & 1) &&
        ai_order_target_not_enemy_inl(pCandidate, pSource))
    {
      switch (pCandidate->wType) {
      case 0x23:
      case 0x24:
      case 0x3B:
      case 0x61:
        return FALSE;
      }

      {
        CSprite * __restrict pSprite = pCandidate->pSprite;
        upos_t y = PosU(pSprite->wPosY);
        upos_t x = pSprite->wPosX;

        dword_665D28 = 0;
        dword_665D24 = 0x6C;
        find_first_unit_near(x, y, 0x60, sub_4321E0, pSource);
      }

      if (dword_665D30 == 0 && dword_665D28 < 0xFA)
        return FALSE;

      {
        CSprite * __restrict pSprite = pCandidate->pSprite;
        upos_t y = PosU(pSprite->wPosY);
        upos_t x = pSprite->wPosX;

        dword_665D2C = 0;
        find_first_unit_near(x, y, 0x60, ai_query_area_ally_hp, pSource);
      }

      return dword_665D2C * 2 < dword_665D28;
    }
  }

  return FALSE;
}

// sub_431B70's filter: files each visible, targetable enemy in range into the
// bucket CUnit::sub_4319D0 scores it, up to 16 per bucket. Always FALSE.
BOOL __fastcall sub_432280(CUnit *pCandidate, CUnit *pSource)
{
  if (pCandidate == pSource)
    return FALSE;
  if (!SpriteSeenBy(pCandidate->pSprite, pSource->bPlayer))  // Hyperion: and slots past 7
    return FALSE;
  if (!pSource->CanTarget(pCandidate, 1))
    return FALSE;
  if (!pSource->IsEnemyUnit(pCandidate))
    return FALSE;
  if (dword_665D20 && pSource->IsWithinTargetRange(dword_665D20, pCandidate))
    return FALSE;
  if (!pSource->IsWithinTargetRange(dword_665D1C, pCandidate))
    return FALSE;

  CUnit *pSub = pSource->subunit;
  CUnit *pAttacker = pSource->IsTurret(pSub) ? pSub : pSource;
  if (!((pAttacker->dwFlags >> 16) & 1)) {
    // The type is read between the two coordinates: that is what keeps it in
    // EDI and spills pCandidate.
    CSprite *pSpr = pCandidate->pSprite;
    upos_t y = pSpr->wPosY;
    WORD wT = pAttacker->wType;
    upos_t x = pSpr->wPosX;
    if (!pAttacker->IsInAttackAngle(gubUnitWeaponGnd[wT],
                                    PosS(x), PosS(y)))
      return FALSE;
  }
  if (!ai_target_unreachable(pSource, pCandidate)) {
    int nBucket = pSource->sub_4319D0(pCandidate);
    DWORD nCount = sgnTargets[nBucket];
    if (nCount < 16) {
      dword_665B70[nBucket][nCount] = pCandidate;
      sgnTargets[nBucket] = nCount + 1;
    }
  }
  return FALSE;
}

// stru_665D14 -- point16_t @0x665D14, sheet name; dword_665D18 follows. The
// harvest search's origin, read as two signed words.
#ifndef stru_665D14
#ifdef OC_DLL_MODE
#define stru_665D14 (*reinterpret_cast<mpoint_t *>(OC_VA(0x665D14u)))
#else
extern mpoint_t stru_665D14;  // Hyperion: a map point
#endif
#endif

// dword_665D10 -- DWORD @0x665D10, sheet name; the harvest search's distance
// bound, doubled before the compare.
#ifndef dword_665D10
#ifdef OC_DLL_MODE
#define dword_665D10 (*reinterpret_cast<int *>(OC_VA(0x665D10u)))
#else
extern int dword_665D10;
#endif
#endif

// Whether a mineral field is worth sending pHarvester to: free, reachable, on
// the origin's height level, not yet explored by pHarvester's owner, and within
// twice the search distance.
BOOL __fastcall sub_432420(CUnit *pUnit, CUnit *pHarvester)
{
  if (!pUnit->IsMineralSrc() || sub_412C90(pUnit))
    return FALSE;
  {
    upos_t y = pUnit->pSprite->wPosY;
    upos_t x = pUnit->pSprite->wPosX;
    if (!sai_path_can_reach_target(pHarvester, x, y))
      return FALSE;
  }
  int nHeight = get_map_height(stru_665D14.x, stru_665D14.y);
  {
    upos_t y = pUnit->pSprite->wPosY;
    upos_t x = pUnit->pSprite->wPosX;
    if (nHeight != get_map_height(PosS(x), PosS(y)))
      return FALSE;
  }
  CSprite *pSpr = pUnit->pSprite;
  // Hyperion: a slot past 7 keeps its explored bits in the port's mask, where 1.17's
  // bit (player + 8) is one of the tile's flags (stage D)
  const int nTile = pSpr->wPosY / 32 * gwMapWidth + PosS(pSpr->wPosX) / 32;
  if (g_hxPlayers && pHarvester->bPlayer >= PLAYER_LOBBY_117) {
    if (!VisionHxTileExplored(nTile, pHarvester->bPlayer))
      return FALSE;
  } else if (gpSquareMap[nTile] & (1 << (pHarvester->bPlayer + 8))) {
    return FALSE;
  }
  upos_t y = pHarvester->pSprite->wPosY;
  upos_t x = pHarvester->pSprite->wPosX;
  int nDistance = sai_path_compute_distance(pUnit, x, y);
  return dword_665D10 * 2 >= nDistance;
}

// gUnitAddOnOffset -- point16_t[96] @0x522AB8, indexed [wType - 106]; spelled as
// in placebox.cpp, whose banner derives the bias the oracle bakes.
#ifndef gUnitAddOnOffset
#ifdef OC_DLL_MODE
#define gUnitAddOnOffset (reinterpret_cast<point16_t *>(OC_VA(0x522AB8u)))
#else
extern point16_t gUnitAddOnOffset[96] OC_UNALIGNED;
#endif
#endif

// Whether pCandidate is a neutral addon of the type pSource builds, sitting on
// pSource's addon cell.
BOOL __fastcall sub_4317D0(CUnit *pCandidate, CUnit *pSource)
{
  if (!(gulUnitTypeFlags[pCandidate->wType] & 2))
    return FALSE;
  if (pCandidate->bPlayer != 11)
    return FALSE;
  // A named copy stops MSVC reusing the pre-call load after the call.
  WORD wSrcType = pSource->wType;
  if (TTBaseUnitType(pCandidate->wType) != wSrcType)
    return FALSE;

  // A byte offset rather than an index: the oracle spills the type pre-scaled.
  // Naming the row as a pointer or reference loses the pre-scale.
  int nSrcOff = pSource->wType * sizeof(point16_t);
  CSprite *pSrc = pSource->pSprite;
  int nType = pCandidate->wType;
  CSprite *pSpr = pCandidate->pSprite;
  if (PosS(pSrc->wPosX - reinterpret_cast<const point16_t *>(
          reinterpret_cast<const BYTE *>(gUnitPlaceboxSize) + nSrcOff)->x / 2) / 32 ==
      (PosS(pSpr->wPosX - gUnitPlaceboxSize[nType].x / 2) -
       gUnitAddOnOffset[nType - 106].x) / 32 &&
      PosS(pSrc->wPosY - reinterpret_cast<const point16_t *>(
          reinterpret_cast<const BYTE *>(gUnitPlaceboxSize) + nSrcOff)->y / 2) / 32 ==
      (PosS(pSpr->wPosY - gUnitPlaceboxSize[nType].y / 2) -
       gUnitAddOnOffset[nType - 106].y) / 32)
    return TRUE;
  return FALSE;
}

// Adds the players whose detector pCandidate reveals pCtx->pSource to. Always
// FALSE, so the search never stops early.
// noinline keeps CUnit::sub_4316B0's direct call. The player read in merged arms
// under a sprite test keeps the visibility test's sprite in EAX and bit in DL.
__declspec(noinline) BOOL __fastcall sub_4318F0(CUnit *pCandidate, vision_ctx_t *pCtx)
{
  CUnit *pSource = pCtx->pSource;
  if (!(pCandidate->dwFlags & 0x40000000) && pCandidate->sub_431A90() &&
      pCandidate != pSource &&
      SpriteSeenBy(pSource->pSprite, pCandidate->bPlayer)) {  // Hyperion: and slots past 7
    int nRange = (pCandidate->dwFlags & 2) ? 0xE0 : pCandidate->sub_423150(0) << 5;
    if (pCandidate->IsWithinTargetRange(nRange, pSource)) {
      int nPlayer;
      if (pSource->pSprite)
        nPlayer = pCandidate->bPlayer;
      else
        nPlayer = pCandidate->bPlayer;
      pCtx->dwMask |= (1 << nPlayer) | SharedVision[nPlayer] | UnitParasite(pCandidate).Low32();  // Hyperion
    }
  }
  return FALSE;
}

// Picks from one survey bucket: the sole entry, else for a 0x2F unit with
// dwUnion134 set the toughest, else the nearest.
CUnit *__fastcall AIPickBestTarget(CUnit *pUnit, int nBucket)
{
  CUnit **pBucket = dword_665B70[nBucket];
  DWORD nCount = sgnTargets[nBucket];
  if (nCount == 1)
    return pBucket[0];

  DWORD nBest = 0;
  if (pUnit->dwUnion134 != 0 && pUnit->wType == 0x2F) {
    DWORD dwMost = pBucket[0]->GetToughness();
    for (DWORD i = 1; i < nCount; ++i) {
      CUnit *pTarget = pBucket[i];
      DWORD dwToughness = ((pTarget->lHitPoints + 0xFF) >> 8) +
          (gbUnitHasShields[pTarget->wType] ? ((LONG)pTarget->lShields >> 8) : 0);
      if (dwToughness > dwMost) {
        dwMost = dwToughness;
        nBest = i;
      }
    }
    return pBucket[nBest];
  }

  DWORD dwNearest;
  {
    CSprite *pTo = pBucket[0]->pSprite;
    CSprite *pFrom = pUnit->pSprite;
    upos_t wToY = pTo->wPosY;
    upos_t wFromY = pFrom->wPosY;
    upos_t wToX = pTo->wPosX;
    // X re-reads pSprite through pUnit; that second reference places the registers.
    upos_t wFromX = pUnit->pSprite->wPosX;
    dwNearest = veclen(PosS(wFromX), PosS(wToX),
                       PosS(wFromY), PosS(wToY));
  }
  for (DWORD i = 1; i < sgnTargets[nBucket]; ++i) {
    CSprite *pTo = pBucket[i]->pSprite;
    CSprite *pFrom = pUnit->pSprite;
    upos_t wToY = pTo->wPosY;
    upos_t wFromY = pFrom->wPosY;
    upos_t wToX = pTo->wPosX;
    upos_t wFromX = pUnit->pSprite->wPosX;
    DWORD dwDistance = veclen(PosS(wFromX), PosS(wToX),
                              PosS(wFromY), PosS(wToY));
    if (dwDistance < dwNearest) {
      dwNearest = dwDistance;
      nBest = i;
    }
  }
  return pBucket[nBest];
}

// A medic's heal target: with dwUnion134 clear, the generic search; otherwise
// the nearest healable unit of the player that no unit is already healing.
// Each order arm compares its own target and continues; one shared pTarget
// local gives i EDX where the oracle has EAX.
CUnit *CUnit::FindHealTarget()
{
  static CUnit *apCandidates[POOL_UNITS_STORE];  // Hyperion: on the stack in 1.17

  if (!TechUseAllowed(0x22, bPlayer, this))
    return NULL;
  if (dwUnion134 == 0)
    return sub_442BC0(0xA0, ai_test_best_heal_target, this);

  DWORD nCount = 0;
  CUnit *pHealer = NULL;
  for (CUnit *pUnit = GetPlayerUnitList(bPlayer); pUnit != NULL;
       pUnit = pUnit->pPlayerUnitNext) {
    if (pUnit->IsDeadOrDying() || (pUnit->pSprite->flags.b & 0x20) ||
        !(pUnit->dwFlags & 1) || pUnit->_unmapped_106[1] != 0 ||
        !sub_437060(pUnit) || (pUnit->dwFlags & 0x40000000))
      continue;
    if (pHealer == NULL && pUnit->wType == 0x22)
      pHealer = pUnit;
    if (gulUnitTypeFlags[pUnit->wType] & 0x10000)
      apCandidates[nCount++] = pUnit;
  }

  for (CUnit *pUnit = pHealer; pUnit != NULL; pUnit = pUnit->pPlayerUnitNext) {
    if (pUnit->bOrderId != 0xB0 &&
        (pUnit->pOrderQueueHead == NULL || pUnit->GetFirstQueuedOrder() != 0xB0))
      continue;
    for (DWORD i = 0; i < nCount; ++i) {
      if (pUnit->bOrderId == 0xB0) {
        if (pUnit->pOrderTarget != apCandidates[i])
          continue;
      } else {
        order_target_t t = pUnit->pOrderQueueHead->target;
        if (t.pUnit != apCandidates[i])
          continue;
      }
      apCandidates[i] = NULL;
      break;
    }
  }

  DWORD dwNearest = 99999999;
  CUnit *pNearest = NULL;
  for (DWORD i = 0; i < nCount; ++i) {
    CUnit *pCandidate = apCandidates[i];
    if (pCandidate != NULL) {
      CSprite *pFrom = pSprite;
      upos_t wToY = pCandidate->pSprite->wPosY;
      upos_t wFromX = pFrom->wPosX;
      upos_t wFromY = pFrom->wPosY;
      upos_t wToX = pCandidate->pSprite->wPosX;
      DWORD dwDistance = veclen(PosS(wFromX), PosS(wToX),
                                PosS(wFromY), PosS(wToY));
      if (dwDistance < dwNearest) {
        dwNearest = dwDistance;
        pNearest = pCandidate;
      }
    }
  }
  return pNearest;
}

// AI mind-control filter: a targetable non-hero that is a loaded transport, one
// of sixteen fixed types, or at least as costly as type 61 in both resources.
BOOL __fastcall ai_test_best_mind_control_target(CUnit *pCandidate, CUnit *pSource)
{
  int nType;

  if (!sub_433DF0(pSource, pCandidate))
    return FALSE;

  nType = pCandidate->wType;
  if (gulUnitTypeFlags[nType] & 0x40)
    return FALSE;

  if ((nType == 0x45 || nType == 0x0B) && pCandidate->IsUnitInside())
    return TRUE;

  switch (nType)
  {
  case 5:
  case 9:
  case 12:
  case 30:
  case 34:
  case 39:
  case 42:
  case 45:
  case 46:
  case 62:
  case 63:
  case 67:
  case 71:
  case 72:
  case 83:
  case 103:
    return TRUE;
  }

  if (guwUnitMineralsCost[nType] >= guwUnitMineralsCost[61] &&
      guwUnitGasesCost[nType] >= guwUnitGasesCost[61])
    return TRUE;

  return FALSE;
}

// ai_order_target_not_enemy_inl plus a test that the target is flagged +0x04.
static __forceinline BOOL ai_order_target_not_enemy_f4(CUnit *pUnit, CUnit *pSource)
{
  CUnit *pTarget = pUnit->pOrderTarget;

  if (pTarget == NULL)
    return FALSE;

  if (!PlayerIsLobby(pTarget->bPlayer))  // Hyperion: >= 8 in 1.17
    return FALSE;

  if (!(pTarget->dwFlags & 4))
    return FALSE;

  return !pSource->IsEnemyUnit(pTarget);
}

// AI disruption-web filter. The 35 no-op labels are needed for the two-level
// jump table; four labels alone compile to compares.
BOOL __fastcall ai_test_best_disruptor_target(CUnit *pCandidate, CUnit *pSource)
{
  DWORD dwFlags;

  if (!sub_433DF0(pSource, pCandidate))
    return FALSE;

  if (pCandidate->wType == 0x7D && pCandidate->IsUnitInside())
  {
    CUnit *pInside = pCandidate->GetFirstTransportUnit();

    if (pInside != NULL && !ai_order_target_not_enemy_f4(pInside, pSource))
      return FALSE;
  }
  else
  {
    if (!ai_order_target_not_enemy_f4(pCandidate, pSource))
      return FALSE;
  }

  dwFlags = pCandidate->dwFlags;

  if (dwFlags & 0x8000)
    return FALSE;

  if (!(dwFlags & 4) && pSource->IsWithinTargetRange(0x100, pCandidate))
    return TRUE;

  switch (pCandidate->wType)
  {
  case 0x7C: case 0x7D: case 0x90: case 0xA2:
    return TRUE;

  case 0x7E: case 0x7F: case 0x80: case 0x81: case 0x82: case 0x83:
  case 0x84: case 0x85: case 0x86: case 0x87: case 0x88: case 0x89:
  case 0x8A: case 0x8B: case 0x8C: case 0x8D: case 0x8E: case 0x8F:
  case 0x91: case 0x92: case 0x93: case 0x94: case 0x95: case 0x96:
  case 0x97: case 0x98: case 0x99: case 0x9A: case 0x9B: case 0x9C:
  case 0x9D: case 0x9E: case 0x9F: case 0xA0: case 0xA1:
    return FALSE;
  }

  return FALSE;
}

// AI dark-swarm filter: the candidate is chasing a friendly unit whose attacker
// fires a type-5 weapon, and the candidate itself can be hit.
BOOL __fastcall ai_test_best_swarm_target(CUnit *pCandidate, CUnit *pSource)
{
  CUnit *pTarget;
  CUnit *pAttacker;
  weapon_id_t cWeapon;

  if (!sub_433DF0(pSource, pCandidate))
    return FALSE;

  if (!ai_order_target_not_enemy_inl(pCandidate, pSource))
    return FALSE;

  pTarget = pCandidate->pOrderTarget;

  if (pTarget->subunit != NULL)
    return FALSE;

  if (pTarget->dwFlags & 4)
    return FALSE;

  cWeapon = pTarget->GetWeaponGndEx();

  if (cWeapon == WEAPON_NONE)
    return FALSE;

  if (gubWeapFireType[cWeapon] != 5)
    return FALSE;

  pAttacker = pCandidate->subunit;

  if (!pCandidate->IsTurret(pAttacker))
    pAttacker = pCandidate;

  cWeapon = pAttacker->GetWeaponGndEx();

  if (cWeapon == WEAPON_NONE)
    return FALSE;

  switch (gubWeapFireType[cWeapon])
  {
  case 0: case 1: case 2: case 4: case 7:
    return pAttacker->GetChanceToHit(pTarget) == 0;

  case 3: case 5: case 6:
    return FALSE;
  }

  return FALSE;
}

// FindFreeMineral needs an int radius (it zero-extends a WORD before the push) and
// both pass CUnit * filters; find.h's view has a WORD radius and a void *
// context. C linkage makes the two views one symbol, @sub_442CC0@20.
extern "C" CUnit *__fastcall sub_442CC0(spos_t x, spos_t y, int nRadius,
                                        CUnitSearchPredicate pfnFilter, CUnit *pContext);

// Finds a mineral field near pt for a harvester once any resource depot exists.
CUnit *CUnit::sub_431F70(mpoint_t pt)
{
  if (!count_get_completion(0xB0, 11) && !count_get_completion(0xB1, 11) &&
      !count_get_completion(0xB2, 11))
    return NULL;
  stru_665D14 = pt;
  dword_665D10 = 0x180;
  return sub_442CC0(pt.x, pt.y, 0x180, sub_432420, this);
}

// The same search through sub_432510, with a wider radius when nFlag is set.
// The radius is an int stored whole and passed as a WORD.
CUnit *CUnit::FindFreeMineral(mpoint_t pt, int nFlag)
{
  if (!count_get_completion(0xB0, 11) && !count_get_completion(0xB1, 11) &&
      !count_get_completion(0xB2, 11))
    return NULL;
  dword_665D18 = nFlag;
  int nRange = nFlag ? 0x200 : 0x100;
  dword_665D10 = nRange;
  stru_665D14 = pt;
  return sub_442CC0(pt.x, pt.y, (WORD)nRange, sub_432510, this);
}

// gubUnitUnmaskTbl -- BYTE[228] @0x521AD8, registered; the units.dat sight range.
#ifndef gubUnitUnmaskTbl
#ifdef OC_DLL_MODE
#define gubUnitUnmaskTbl (reinterpret_cast<BYTE *>(OC_VA(0x521AD8u)))
#else
extern BYTE gubUnitUnmaskTbl[228] OC_UNALIGNED;
#endif
#endif

// Surveys the area for attack targets and picks from the first non-empty bucket.
CUnit *CUnit::FindAttackTarget()
{
  memset(sgnTargets, 0, 6 * sizeof(DWORD));
  DWORD nRange = EnemyTrackingRange();
  if (dwFlags & 0x20)
    nRange += 2;
  else if (dwUnion134 != 0) {
    DWORD nSeek = gubUnitUnmaskTbl[wType];
    if (nSeek > nRange)
      nRange = nSeek;
  }
  dword_665D1C = nRange << 5;
  AISetMinRange();
  find_first_unit_near(pSprite->wPosX, pSprite->wPosY, dword_665D1C + 0x40, sub_432280, this);
  for (unsigned i = 0; i < 6; ++i) {
    if (sgnTargets[i] != 0)
      return AIPickBestTarget(this, i);
  }
  return NULL;
}

// Finds the neutral addon on this building's addon cell: a search over the
// placement box grown by 0x20 on every side.
CUnit *CUnit::sub_431AE0()
{
  mrect_t  rc;
  CSprite *pSpr = pSprite;
  WORD t = wType;
  short w = gUnitPlaceboxSize[t].x;
  rc.left = pSpr->wPosX - w / 2 - 0x20;
  short h = gUnitPlaceboxSize[t].y;
  rc.top = pSpr->wPosY - h / 2 - 0x20;
  rc.right = rc.left + 0x40 + w;
  rc.bottom = rc.top + 0x40 + h;
  return sub_442B60(&rc, sub_4317D0, this);
}
