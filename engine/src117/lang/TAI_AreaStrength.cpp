// TAI_AreaStrength.cpp -- the town AI's area threat model: the per-unit-type
// air and ground threat base table, the per-unit strength and threat scoring
// that feeds it, and the greatest-threat queries. Bodies are in oracle VA
// order.

#include "oc_data.h"  // Hyperion: OC_VA
#include "../h/TAI_AreaStrength.h"
#include "../h/CUnit.h"         // class CUnit, its members
#include "../h/CUnitFind.h"     // get_units_in_region, finish_unit_iteration
#include "../h/CUnitMisc.h"     // free IsEnemyUnit(int, CUnit *)
#include "../h/CUnitSearches.h" // GetRegion_CUnit, aiscript_has_campaign
#include "../h/CUnitCombat.h"   // ai_target_unreachable
#include "../h/CTarget.h"       // CTarget::XYInRadius
#include "../h/order.h"         // get_units_in_rect
#include "../h/sai.h"           // GetRegionBounds
#include "../h/SAI_Banker.h"    // GetRegion_xy
#include "../h/SAI_Main.h"      // tai_compute_unit_threat_scores (the contract)
#include "../h/TAI_Captain.h"   // tai_soldier_captain_air_threat, tai_soldier_captain_ground_threat
#include "../h/TAI_GuardPost.h" // TGuard: the AI record a unit's +0x134 points at
static_assert(offsetof(TGuard, bType) == offsetof(TSoldier, bType), "the AI records' kind byte");
#include <math.h>

#include "../gen/globals117.h"
#include "../h/playervision.h"  // Hyperion: the slots past 31 (stage D)
#include "../h/handicap.h"  // Hyperion: a player's handicap

// The globals this TU reaches, dual-spelled, one `#ifndef` per name.
//
// 0x6AC530 sgnThreatBase_g is unregistered and holds two rows of 228 DWORDs:
// row 0 at 0x6AC530 is the air threat per unit type and row 1 at 0x6AC8C0 the
// ground threat (0x6AC8C0 - 0x6AC530 == 0x390 == 228 DWORDs). Its only writer,
// tai_compute_unit_threat_scores, is one flat 228-trip loop storing to
// `[esi*4+6AC530h]` and `[esi*4+6AC8C0h]`, which is where both the stride and
// the row count come from; the next sheet name, gSoldiers_g @0x6ACC50, closes
// the span. It is declared flat because the generator rejects a
// multi-dimensional array and a 2-D `DWORD t[2][228]` computes addresses
// differently, so the 228 stride lives in the subscript.
#ifndef OC117_SGNTHREATBASE_G
#define OC117_SGNTHREATBASE_G
#ifdef OC_DLL_MODE
#define sgnThreatBase_g (reinterpret_cast<DWORD *>(OC_VA(0x6AC530u)))
#else
extern DWORD sgnThreatBase_g[456];
#endif
#endif

// Forward declarations for this TU. src117/h/TAI_AreaStrength.h, included
// above, is the contract, and C2556 catches a mismatch at compile time.

DWORD tai_compute_splash_damage_threat(int nUnitType, DWORD dwThreat);

DWORD tai_compute_hit_point_threat(int nUnitType, BYTE bWeapon);

CUnit *tai_get_greatest_threat(int nPlayer, CUnit *pUnit, int nRegion,
                               CUnit **ppUnits, int nAirGround);

DWORD tai_sum_threat_scores_region(int nPlayer, int nRegion, int nAirGround);

int tai_compute_threat_priority(CUnit *pUnit, int nAirGround);

DWORD tai_get_special_threat_scores(CUnit *pUnit, int nAirGround, DWORD nScore);

int tai_get_threat_score(CUnit *pUnit, int nAirGround);

DWORD tai_compute_unit_strength(CUnit *pUnit, int nAirGround);

CUnit *tai_get_greatest_air_threat_near(CUnit *pUnit);
CUnit *tai_get_greatest_ground_threat_near(CUnit *pUnit);

int tai_get_air_threat_base(int nUnitType);
int tai_get_ground_threat_base(int nUnitType);

CUnit *tai_get_greatest_air_threat_region(int nPlayer, int nRegion);
CUnit *tai_get_greatest_ground_threat_region(int nPlayer, int nRegion);

WORD tai_get_total_ground_threat_region(int nPlayer, int nRegion);
WORD tai_get_total_air_threat_region(int nPlayer, int nRegion);

// 0x4A8F30 tai_assign_unit_strength (38 B). Caches both strengths in the unit:
//   xor edx, edx / call tai_compute_unit_strength / mov word ptr [esi+138h], ax
//   mov edx, 1h  / call tai_compute_unit_strength / mov word ptr [esi+13Ah], ax
// The first call keeps the incoming ECX -- `mov esi, ecx` does not clobber it.
// +0x138 and +0x13A are the two unsigned WORDs CUnit::AirStrength and
// CUnit::GroundStrength read back; this is the writer side of that pair.
void tai_assign_unit_strength(CUnit *pUnit)
{
  *reinterpret_cast<WORD *>(&pUnit->_unmapped_138[0x00]) =
      static_cast<WORD>(tai_compute_unit_strength(pUnit, 0));
  *reinterpret_cast<WORD *>(&pUnit->_unmapped_138[0x02]) =
      static_cast<WORD>(tai_compute_unit_strength(pUnit, 1));
}

// 0x4A8F60 tai_compute_splash_damage_threat (54 B). A two-level switch on the
// unit type: a 79-byte case-index array at 0x4A8FB0 covering types 7..85 and a
// six-entry jump table at 0x4A8F98, both in the COMDAT tail. The groups, read
// out of the oracle image:
//
//   group 0  shr edx,2h         types  7, 41, 64
//   group 1  xor eax,eax        types 13, 73, 85
//   group 2  lea eax,[edx+edx]  types 32, 43, 65
//   group 3  shr edx,4h         types 47, 50
//   group 4  the /10 magic      type  83
//   group 5  mov eax,edx        everything else in 7..85
//
// `jnbe $+0x2Bh` sends the out-of-range types to group 5 as well, so group 5 is
// the `default` arm. dwThreat is unsigned: `shr` not `sar`, and the /10
// is the unsigned magic `mov eax, 0CCCCCCCDh / mul edx / shr edx, 3h`.
DWORD tai_compute_splash_damage_threat(int nUnitType, DWORD dwThreat)
{
  switch (nUnitType)
  {
  case 7:
  case 41:
  case 64:
    return dwThreat / 4;

  case 13:
  case 73:
  case 85:
    return 0;

  case 32:
  case 43:
  case 65:
    return dwThreat * 2;

  case 47:
  case 50:
    return dwThreat / 16;

  case 83:
    return dwThreat / 10;
  }
  return dwThreat;
}

// 0x4A9000 tai_compute_hit_point_threat (195 B). sqrt(hit points x damage
// throughput) scaled by 7.58, the per-unit-type constant that seeds the air and
// ground threat table.
//
// The four weapon tables are read inline, twice each, and left to CSE: four
// typed locals cost 4 bytes (see the note in the body). Two divides, two
// signednesses, both from the registry types: `div edi` is unsigned because
// wpn_maxrange_g is DWORD, `cdq / idiv [esp+14h]` signed because WORD*BYTE<<11
// is int. `cmp bl, -7Eh` is weapon 130, whose damage term is forced to zero.
//
// The tail is a double round trip: `movd xmm0, eax / cvtdq2pd / shr eax, 1Fh /
// addsd [eax*8+4EE7E0h]` is MSVC's (double)(unsigned) idiom, 0x4EE7E0 holding
// {0.0, 4294967296.0}; then _libm_sse2_sqrt_precise, `mulsd [0x4F0598] / divsd
// [0x4F0590]` for 758.0 and 100.0, and _dtoui3, double to unsigned, which is
// why this returns DWORD. It is the first floating-point body in src117.
DWORD tai_compute_hit_point_threat(int nUnitType, weapon_id_t bWeapon)
{
  DWORD dwShields;
  int nHitPoints;
  DWORD dwThroughput;
  DWORD dwTotal;
  int nBurst;

  if (gbUnitHasShields[nUnitType] != 0)
    dwShields = guwUnitEnergy[nUnitType];
  else
    dwShields = 0;

  nHitPoints = (gxUnitLife[nUnitType] >> 8) + dwShields;
  if (nHitPoints == 0)
    return 0;

  // The four weapon tables are read inline and left to CSE -- measured.
  // Hoisting them into four typed locals costs 4 bytes: MSVC then spills the
  // cooldown before the divide and divides by memory instead of `div edi`, and
  // reassociates the throughput product. Read inline, the allocator makes the
  // oracle's choice.
  dwThroughput = wpn_maxrange_g[bWeapon] / gnWeapCooldown[bWeapon]
                 * gnWeapLaunch[bWeapon] * wpn_dmgAmt_g[bWeapon];

  if (bWeapon == WEAPON_NONE)
    nBurst = 0;
  else
    nBurst = (wpn_dmgAmt_g[bWeapon] * gnWeapLaunch[bWeapon] << 11)
             / gnWeapCooldown[bWeapon];

  dwTotal = (static_cast<DWORD>(nBurst * nHitPoints) >> 8) + dwThroughput;
  return static_cast<DWORD>(sqrt(static_cast<double>(dwTotal)) * 758.0 / 100.0);
}


// 0x4A90D0 tai_get_greatest_threat (598 B), `ret Ch`. The town AI's target
// picker: walk a unit list and keep the highest tai_compute_threat_priority
// score. It returns the best unit -- the epilogue is
// `mov eax, dword ptr [ebp-1Ch]`, the slot the body writes and initialises --
// and all four region and near wrappers tail-forward it.
//
// [ebp+08h] is the region id on entry and the list cursor afterwards: the
// parameter slot is reused, and the frame `sub esp, 20h` is exactly the eight
// [ebp-4..-20h] slots, so a ninth local would make it 24h. [ebp-0Ch] is
// pUnit->dwUnion134 or NULL, not a constant: the `xor eax, eax` at +0x37 is
// inside the pUnit == NULL arm and the store at +0x40 is the join.

// IsEnemyUnit here is the free function at src117/h/CUnitMisc.h:69, not the
// CUnit member of the same name at src117/h/CUnit.h:3146. ECX carries the
// player index, not a unit.
CUnit *tai_get_greatest_threat(int nPlayer, CUnit *pUnit, int nRegion,
                               CUnit **ppUnits, int nAirGround)
{
  int nWType;
  int nUnitRegion;
  const BYTE *pAI;
  CUnit **ppCur;
  CUnit *p;
  CUnit *pBest;
  int nBestScore;
  int nScore;
  DWORD dwFlags;
  int nCurShields;
  int nMaxShields;
  int nMaxLife;
  int nLife;
  int nX;
  int nY;

  if (pUnit != NULL)
  {
    nWType = pUnit->wType;
    nUnitRegion = GetRegion_CUnit(pUnit);
    pAI = reinterpret_cast<const BYTE *>(pUnit->dwUnion134);
  }
  else
  {
    nUnitRegion = nRegion;
    nWType = 228;
    pAI = NULL;
  }

  nBestScore = -1;
  pBest = NULL;
  ppCur = ppUnits;

  for (p = *ppCur; p != NULL; p = *++ppCur)
  {
    if (!IsEnemyUnit(nPlayer, p) && !p->IsCritter())
      continue;

    dwFlags = p->dwFlags;
    if ((dwFlags & 0x4000000) != 0)
      continue;

    if ((dwFlags & 0x300) != 0 && !UnitDetectedBy(p, nPlayer))  // Hyperion: every slot (stage D)
      continue;

    if ((dwFlags & 0x40000000) != 0)
    {
      if (gbUnitHasShields[p->wType] != 0)
        nCurShields = static_cast<int>(p->lShields) >> 8;
      else
        nCurShields = 0;

      if (gbUnitHasShields[p->wType] != 0)
        nMaxShields = HandicapShields(guwUnitEnergy[p->wType], p->bPlayer);  // Hyperion: at the owner's handicap
      else
        nMaxShields = 0;

      nMaxLife = HandicapLife(gxUnitLife[p->wType], p->bPlayer) >> 8;  // Hyperion: at the owner's handicap
      if (nMaxLife == 0)
      {
        nLife = static_cast<int>(p->lHitPoints + 0xFF) >> 8;
        nMaxLife = (nLife != 0) ? nLife : 1;
      }

      if ((static_cast<int>(p->lHitPoints + 0xFF) >> 8) + nCurShields
          < nMaxShields + nMaxLife)
        continue;
    }

    if (pUnit != NULL && !pUnit->CanTarget(p, 1))
      continue;

    if (p->IsCritter() && aiscript_has_campaign(nPlayer))
      continue;

    if (pUnit != NULL && ai_target_unreachable(pUnit, p))
      continue;

    // Hyperion: the record's fields by TGuard's offsets, +0x08, +0x12 and
    // +0x14 in 1.17.
    if (pAI != NULL && pAI[offsetof(TGuard, bType)] == 1)
    {
      nX = PosS(reinterpret_cast<const TGuard *>(pAI)->wPosX);  // Hyperion: by field (stage E)
      nY = PosS(reinterpret_cast<const TGuard *>(pAI)->wPosY);
      if (GetRegion_CUnit(p) != GetRegion_xy(nX, nY))
      {
        if (!reinterpret_cast<CTarget *>(p)->XYInRadius(0xC0, nX, nY))
          continue;
        if (!reinterpret_cast<CTarget *>(pUnit)->XYInRadius(0xC0, nX, nY))
          continue;
      }
    }
    else if (GetRegion_CUnit(p) != nUnitRegion)
    {
      if (!pUnit->IsWithinTargetRange(0x120, p))
        continue;
    }

    nScore = tai_compute_threat_priority(p, nAirGround);
    if (nWType == 8 && p->sub_431A90() && (p->dwFlags & 0x20000) != 0)
      nScore += 1000;

    if (nScore > nBestScore)
    {
      nBestScore = nScore;
      pBest = p;
    }
  }

  finish_unit_iteration(ppUnits);
  return pBest;
}

// 0x4A9330 tai_sum_threat_scores_region (293 B), `ret 4h`. Sums the strength of
// every enemy unit in a region that matches the air/ground selector. It returns
// a value, not void: the epilogue is `mov eax, edi` with EDI the accumulator,
// and both 8-byte tai_get_total_*_threat_region wrappers tail-forward it.
//
// This body depends on tai_compute_unit_strength being in this TU: the oracle
// inlines the nAirGround == 0 call, splicing the whole specialised body in and
// ending `div ecx / mov edi, eax`, and leaves the nAirGround == 1 call a real
// `call`. The source writes both calls and MSVC's inline budget does the rest,
// but moving the callee out of the TU, or marking it noinline, loses the
// splice.
//
// The selector test is two nested ifs, not a XOR or an equality of two
// booleans: the layout is an if/else where each arm has its own `continue`.
DWORD tai_sum_threat_scores_region(int nPlayer, int nRegion, int nAirGround)
{
  CUnit **pp;
  CUnit **ppList;
  CUnit *p;
  DWORD dwTotal;
  DWORD dwAir;
  DWORD dwGround;

  dwTotal = 0;
  ppList = get_units_in_region(nRegion);
  pp = ppList;

  for (p = *pp; p != NULL; p = *++pp)
  {
    if ((p->dwFlags & 4) != 0)
    {
      if (nAirGround == 0)
        continue;
    }
    else
    {
      if (nAirGround != 0)
        continue;
    }

    if (!IsEnemyUnit(nPlayer, p))
      continue;

    dwAir = tai_compute_unit_strength(p, 0);
    dwGround = tai_compute_unit_strength(p, 1);
    dwTotal += (dwAir > dwGround) ? dwAir : dwGround;
  }

  finish_unit_iteration(ppList);
  return dwTotal;
}

// 0x4A9460 tai_compute_threat_priority (181 B). The per-candidate score
// tai_get_greatest_threat ranks units by: threat x 128 / toughness, with a
// floor of 1, or 2 for a cloaked or burrowed candidate, when the quotient
// underflows to zero. The divide is unsigned, so the quotient is DWORD, and
// `test eax, eax / jnz $-0x4Bh` branches backwards into the `return 0` epilogue
// one instruction past the `xor eax, eax`, so EAX survives: it is
// `if (q != 0) return q;` reusing the shared epilogue, not a second zero exit.
// The last arm is `!!(flags & 0x20000) + 1`.
int tai_compute_threat_priority(CUnit *pUnit, int nAirGround)
{
  DWORD dwScore;
  DWORD dwToughness;
  DWORD dwResult;

  if (pUnit->wType == 35 || pUnit->wType == 36 || pUnit->wType == 59
      || pUnit->wType == 97)
    return 0;

  if ((pUnit->dwFlags & 0x40000000) != 0
      && pUnit->GetToughness() < pUnit->MaxToughness())
    return 0;

  if (pUnit->IsCritter())
    return 1;

  dwScore = tai_get_special_threat_scores(
      pUnit, nAirGround, tai_get_threat_score(pUnit, nAirGround));
  dwToughness = pUnit->GetToughness();

  dwResult = (dwScore << 7) / (dwToughness != 0 ? dwToughness : 1);
  if (dwResult != 0)
    return dwResult;
  // The ternary is deliberate: `((flags & 0x20000) != 0) + 1` lets MSVC take
  // its single-bit peephole, `shr eax, 11h / and eax, 1`, which is shorter and
  // costs 9 bytes overall once the fourth type-compare's forward jz no longer
  // fits in a rel8. The ternary forces the generic boolean conversion the
  // oracle emits.
  return (pUnit->dwFlags & 0x20000) != 0 ? 2 : 1;
}

// 0x4A9520 tai_get_special_threat_scores (147 B), `ret 4h`. A per-unit-type
// bonus added to the running score, through another two-level switch: an
// 84-byte case-index array at 0x4A95D0 covering wType 1..84 and a seven-entry
// jump table at 0x4A95B4. The arms, read out of the oracle image: wType 1 the
// bOrderId window, 1000; wType 2 the BYTE at +0xC0 times 100; wTypes 11, 42,
// 57 and 69 GetTotalOccupiedCargo times 100; wType 30 nScore + nScore/4; wType
// 34 the air/ground table's row 0; wType 84 plus 50; and the default leaves
// nScore alone.
//
// The case order below is the oracle's block order, not the numeric one: the
// jump-table index comes from the lowest case value in a group, but the blocks
// are emitted in source order, 30, 1, 34, 2, {11,42,57,69}, 84. Every arm ends
// `mov eax, ecx`, including the constant one, which is one accumulator written
// by each arm and returned once, so the body assigns to nScore and falls
// through. The +0xC0 read is a BYTE where CUnit.h declares a union of CUnit *.
DWORD tai_get_special_threat_scores(CUnit *pUnit, int nAirGround, DWORD nScore)
{
  switch (pUnit->wType)
  {
  case 30:
    nScore += nScore / 4;
    break;

  case 1:
    if (pUnit->bOrderId >= 0x7B && pUnit->bOrderId <= 0x81)
      nScore = 1000;
    break;

  case 34:
    nScore += sgnThreatBase_g[nAirGround * 228];
    break;

  case 2:
    nScore += reinterpret_cast<const BYTE *>(&pUnit->pUnionC0)[0] * 100;
    break;

  case 11:
  case 42:
  case 57:
  case 69:
    nScore += pUnit->GetTotalOccupiedCargo() * 100;
    break;

  case 84:
    nScore += 50;
    break;
  }
  return nScore;
}

// 0x4A9630 tai_get_threat_score (115 B). The per-unit threat contribution: the
// unit type's base score for this air/ground class, replaced for type 125 by
// its cargo count times the row base, plus half the integer energy for types
// flagged 0x200000 that are not themselves flagged 0x40000000.
//
// The `+=` is deliberate, and sixteen shapes were measured: writing the guarded
// arm as `return nScore + (...)` costs 2 bytes and moves the accumulator from
// ESI to ECX, which then forces a `mov ecx, ebx` before the member call above,
// because ECX no longer holds the incoming `this`. MSVC's allocator runs over
// the whole function, so the tail's shape decides the head's registers.
//
// The energy read is a byte read at +0xA3, not a shift of the WORD at +0xA2:
// wEnergy is x256 fixed point, so the high byte is the integer energy, and
// `(wEnergy >> 8) >> 1` folds to one byte too many. The wType reads are inline
// on purpose -- hoisted into an int local, MSVC CSEs the third read.
int tai_get_threat_score(CUnit *pUnit, int nAirGround)
{
  int nScore;

  nScore = sgnThreatBase_g[nAirGround * 228 + pUnit->wType];
  if (pUnit->wType == 125)
    nScore = pUnit->GetTotalOccupiedCargo() * sgnThreatBase_g[nAirGround * 228];

  if ((gulUnitTypeFlags[pUnit->wType] & 0x200000) != 0
      && (pUnit->dwFlags & 0x40000000) == 0)
    nScore += reinterpret_cast<const BYTE *>(&pUnit->wEnergy)[1] >> 1;

  return nScore;
}

// 0x4A96B0 tai_compute_unit_strength (170 B). Threat times toughness scaled by
// the unit's remaining life, with two early zeroes.
//
// It returns 32 bits, not WORD: the specialised copy MSVC inlines into
// tai_sum_threat_scores_region ends `div ecx / mov edi, eax` with no
// zero-extension, and the surviving call's result is consumed `cmp edi, eax` at
// full width, where a WORD return needs a `movzx eax, ax` at both.
// tai_assign_unit_strength's `mov word ptr [esi+138h], ax` is a store-truncate
// and free under either return type.
//
// wType is re-read, not hoisted: the oracle loads it once for the four type
// compares, one expression, and reloads it with `movzx ecx, word ptr [edi+64h]`
// for the shields lookup. `mov ecx, 1h / cmovnz ecx, eax` off the flags of
// `add eax, esi` is the zero-guard on the divisor.
DWORD tai_compute_unit_strength(CUnit *pUnit, int nAirGround)
{
  DWORD dwScore;
  DWORD dwToughness;
  DWORD dwShields;
  DWORD dwLife;

  if (pUnit->wType == 35 || pUnit->wType == 36 || pUnit->wType == 59
      || pUnit->wType == 97)
    return 0;

  if ((pUnit->dwFlags & 0x40000000) != 0
      && pUnit->GetToughness() < pUnit->MaxToughness())
    return 0;

  dwScore = tai_get_threat_score(pUnit, nAirGround);
  dwToughness = pUnit->GetToughness();

  if (gbUnitHasShields[pUnit->wType] != 0)
    dwShields = HandicapShields(guwUnitEnergy[pUnit->wType], pUnit->bPlayer);  // Hyperion: at the owner's handicap
  else
    dwShields = 0;

  dwLife = pUnit->GetLife() + dwShields;
  return dwToughness * dwScore / (dwLife != 0 ? dwLife : 1);
}

// 0x4A9760 tai_get_greatest_air_threat_near (114 B). Widens the unit's own
// region bounds by 0x120 pixels on every side and asks tai_get_greatest_threat
// for the best air target in the rectangle. dwUnion134 is CUnit.h's DWORD
// placeholder and is dereferenced here at +8 as a BYTE, so it is a pointer in
// this body, spelled through a cast rather than retyping the field. MSVC hoists
// the two 16-bit constants into registers itself, so the source just writes the
// four adjustments.
CUnit *tai_get_greatest_air_threat_near(CUnit *pUnit)
{
  rect16_t rc;
  CUnit **ppUnits;
  BYTE bPlayer;

  // Hyperion: every AI record keeps its kind where TSoldier does, +0x08 in
  // 1.17.
  if (reinterpret_cast<const BYTE *>(pUnit->dwUnion134)[offsetof(TSoldier, bType)] != 4)
  {
    GetRegionBounds(GetRegion_CUnit(pUnit), &rc);
    // The adjustments are interleaved by sign on purpose: as two runs
    // (top, left then bottom, right) MSVC reuses one register for both
    // constants, while interleaved both are live at once, the allocator takes
    // ECX and EAX, and the scheduler regroups the four adds into the oracle's
    // same-register runs. Eleven orderings were swept; only this one matches.
    rc.top -= 0x120;
    rc.bottom += 0x120;
    rc.left -= 0x120;
    rc.right += 0x120;

    ppUnits = get_units_in_rect(&rc);
    bPlayer = pUnit->bPlayer;
    return tai_get_greatest_threat(bPlayer, pUnit, GetRegion_CUnit(pUnit),
                                   ppUnits, 0);
  }
  return tai_soldier_captain_air_threat(pUnit);
}

// 0x4A97E0 tai_get_air_threat_base (8 B) / 0x4A97F0 tai_get_ground_threat_base.
//   mov eax, dword ptr [ecx*4+6AC530h]   <- row 0
//   mov eax, dword ptr [ecx*4+6AC8C0h]   <- row 1, i.e. +228 DWORDs
int tai_get_air_threat_base(int nUnitType)
{
  return sgnThreatBase_g[0 * 228 + nUnitType];
}

int tai_get_ground_threat_base(int nUnitType)
{
  return sgnThreatBase_g[1 * 228 + nUnitType];
}

// 0x4A9800 / 0x4A9820 (29 B each) -- the whole-region form of the two above.
CUnit *tai_get_greatest_air_threat_region(int nPlayer, int nRegion)
{
  CUnit **ppUnits = get_units_in_region(nRegion);

  return tai_get_greatest_threat(nPlayer, NULL, nRegion, ppUnits, 0);
}

CUnit *tai_get_greatest_ground_threat_region(int nPlayer, int nRegion)
{
  CUnit **ppUnits = get_units_in_region(nRegion);

  return tai_get_greatest_threat(nPlayer, NULL, nRegion, ppUnits, 1);
}

// 0x4A9840 / 0x4A9850 (8 B each) -- `push <sel> / call / ret`.
WORD tai_get_total_ground_threat_region(int nPlayer, int nRegion)
{
  return static_cast<WORD>(tai_sum_threat_scores_region(nPlayer, nRegion, 1));
}

WORD tai_get_total_air_threat_region(int nPlayer, int nRegion)
{
  return static_cast<WORD>(tai_sum_threat_scores_region(nPlayer, nRegion, 0));
}

// 0x4A9860 tai_get_greatest_ground_threat_near (114 B) -- identical to the air
// form at 0x4A9760 but with `push 1h` and the 0x4AE870 fallback.
CUnit *tai_get_greatest_ground_threat_near(CUnit *pUnit)
{
  rect16_t rc;
  CUnit **ppUnits;
  BYTE bPlayer;

  // Hyperion: every AI record keeps its kind where TSoldier does, +0x08 in
  // 1.17.
  if (reinterpret_cast<const BYTE *>(pUnit->dwUnion134)[offsetof(TSoldier, bType)] != 4)
  {
    GetRegionBounds(GetRegion_CUnit(pUnit), &rc);
    // The adjustments are interleaved by sign on purpose: as two runs
    // (top, left then bottom, right) MSVC reuses one register for both
    // constants, while interleaved both are live at once, the allocator takes
    // ECX and EAX, and the scheduler regroups the four adds into the oracle's
    // same-register runs. Eleven orderings were swept; only this one matches.
    rc.top -= 0x120;
    rc.bottom += 0x120;
    rc.left -= 0x120;
    rc.right += 0x120;

    ppUnits = get_units_in_rect(&rc);
    bPlayer = pUnit->bPlayer;
    return tai_get_greatest_threat(bPlayer, pUnit, GetRegion_CUnit(pUnit),
                                   ppUnits, 1);
  }
  return tai_soldier_captain_ground_threat(pUnit);
}

// 0x4A98E0 tai_compute_unit_threat_scores (295 B). Fills both rows of the
// 0x6AC530 table in one pass over all 228 units.dat types.
//
// The loop bound is the row count and the sentinel at once: `mov ebx, 0E4h`
// hoists 228 into a register, `cmp ax, bx` tests guwAttachedUnit[nType] == 228
// for "none", and the same register closes the loop -- which is what derives
// the table's 228-DWORD row stride. The air and ground halves are written out
// twice because the oracle emits the eight type compares and the whole
// subject-substitution chain twice, verbatim. The else-if chain is not a
// switch: the oracle compares 72, 82, 83, 81, in target-grouped order, and lays
// the lookup arm first, where a switch on four values sorts them.
void tai_compute_unit_threat_scores()
{
  DWORD nType;
  int nSubject;
  int nWeapon;
  WORD wAttached;
  DWORD dwAir;
  DWORD dwGround;

  for (nType = 0; nType < 228; ++nType)
  {
    if (nType == 35 || nType == 36 || nType == 59 || nType == 97)
    {
      dwAir = 0;
    }
    else
    {
      if (nType == 72 || nType == 82)
        nSubject = 73;
      else if (nType == 83 || nType == 81)
        nSubject = 85;
      else
      {
        wAttached = guwAttachedUnit[nType];
        nSubject = (wAttached == 228) ? nType : wAttached;
      }

      nWeapon = gubUnitWeaponAir[nSubject];
      if (nWeapon == WEAPON_NONE)
        dwAir = 1;
      else
        dwAir = tai_compute_splash_damage_threat(
            nType, tai_compute_hit_point_threat(
                       nType, static_cast<weapon_id_t>(nWeapon)));
    }

    if (nType == 35 || nType == 36 || nType == 59 || nType == 97)
    {
      dwGround = 0;
    }
    else
    {
      if (nType == 72 || nType == 82)
        nSubject = 73;
      else if (nType == 83 || nType == 81)
        nSubject = 85;
      else
      {
        wAttached = guwAttachedUnit[nType];
        nSubject = (wAttached == 228) ? nType : wAttached;
      }

      nWeapon = gubUnitWeaponGnd[nSubject];
      if (nWeapon == WEAPON_NONE)
        dwGround = 1;
      else
        dwGround = tai_compute_splash_damage_threat(
            nType, tai_compute_hit_point_threat(
                       nType, static_cast<weapon_id_t>(nWeapon)));
    }

    sgnThreatBase_g[0 * 228 + nType] = (dwAir == 1 && dwGround > dwAir) ? 0 : dwAir;
    sgnThreatBase_g[1 * 228 + nType] =
        (dwGround == 1 && dwAir > dwGround) ? 0 : dwGround;
  }
}

// 0x4A9A10 CUnit::AirStrength / 0x4A9A20 CUnit::GroundStrength (8 B each).
//   movzx eax, word ptr [ecx+138h]   /   movzx eax, word ptr [ecx+13Ah]
// Both are declared `int` in src117/h/CUnit.h, and the zero-extending 16-bit
// loads are what make +0x138 and +0x13A unsigned WORDs.
int CUnit::AirStrength()
{
  return *reinterpret_cast<const WORD *>(&_unmapped_138[0x00]);
}

int CUnit::GroundStrength()
{
  return *reinterpret_cast<const WORD *>(&_unmapped_138[0x02]);
}
