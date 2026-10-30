// CUnitUpgrade.cpp -- the research and upgrade block: start, cancel, refund and per-tick
// dispatch for both the tech arm and the upgrade arm, the three upgrades.dat cost queries,
// and the unit-type predicates CUnit::sub_43B0B0 re-walks a player's unit list with when
// an upgrade lands. Bodies are in oracle VA order.
//
// TU membership matters in both directions here, and this file has an instance of each.
// CUnit::SetMovementUpgrade is exactly CUnit::ResetMovementSpeed's 65 bytes plus one
// 10-byte `or dword ptr [esi+0DCh], 10000000h`, so it is written as the flag store
// followed by a call and /Ob2 has to splice that callee in: removing ResetMovementSpeed
// from this TU would break it. The other way round, the three cost queries at the bottom
// are 32-bit (`imul eax, ecx`) while refund_upgrade_full and refund_upgrade_partial
// compute the same quantity 16-bit, so the oracle did not call them there and neither
// does this file: the arithmetic is written out literally.

#include "oc_data.h"  // Hyperion: OC_VA
#include "../h/CUnitUpgrade.h"
#include "../h/CUnit.h"
#include "../h/CSprite.h"        // CSprite::SetAnimation
#define get_max_upgrade_level get_max_upgrade_level_int_view
#include "../h/saveload.h"       // ours: get_upgrade_level / get_tech_researched /
                                 // set_tech_researched / set_upgrade_level
#undef get_max_upgrade_level
#include "../h/statbtn.h"        // ours: set/clear_{tech,upgrade}_in_progress
#include "../h/SAI_Banker.h"     // ours: bldg_get_{tech,upgrade}_cost,
                                 // bldg_deduct_resources
#include "../h/gamesnd.h"        // ours: gamesnd_research_complete /
                                 // gamesnd_upgrade_finished
#include "../h/CUnitTerran.h"    // ours: status_set_update, spelled __fastcall
                                 // exactly as five existing headers spell it
#include "../gen/globals117.h"
// DispatchUpgrade's view of get_max_upgrade_level: the player goes in as a 16-bit value.
// saveload.h declares the int view; both are the C symbol @get_max_upgrade_level@8.
// Hyperion: the player is an int, as the definition reads it, which the x64
// build needs; a player number is the same widened.
extern "C" BYTE __fastcall get_max_upgrade_level(int nPlayer, WORD wUpgradeId);

// The globals this TU reaches, dual-spelled, one #ifndef per name: the plain extern arm
// alone leaves the shipped form's baked slots unchecked, and one guard over the whole
// block would go false and undeclare every name under it.

// 0x536720 -- the expansion (Brood War) gate, `cmp byte ptr [0x536720], 0h` in
// CUnit::ArmorUpgrades and CUnit::ApplyUpgrades. It is unregistered and unnamed in the
// sheet, and src117 already carries three TU-local spellings of it; gametype.cpp's
// gbIsExpansion is the one reused here.
#ifndef gbIsExpansion
#ifdef OC_DLL_MODE
#define gbIsExpansion (*reinterpret_cast<BYTE *>(OC_VA(0x536720u)))
#else
extern BYTE gbIsExpansion;
#endif
#endif

// The upgrades.dat and techdata.dat cost columns, and the two per-type lookup tables, all
// under the sheet's own names. The widths are read off the operands: every upgrades and
// techdata access is `movzx ... word ptr [<idx>*2+<base>]` or `imul ax, word ptr [...]`,
// stride 2 and zero-extending, so unsigned WORD, and both per-type tables are
// `movzx ... byte ptr [<idx>+<base>]`, stride 1, so BYTE. The extents, 61 upgrades and 44
// techs, are fixed twice over: by the next name in the sheet (0x80 and 0x58 apart) and by
// the guards `cmp bl, 3Dh` and `cmp bl, 2Ch` in CUnit::StartUpgrade and StartResearch.
#ifndef upg_oreCostBase_g
#ifdef OC_DLL_MODE
#define upg_oreCostBase_g    (reinterpret_cast<WORD *>(OC_VA(0x5248D0u)))
#else
extern WORD upg_oreCostBase_g[61];
#endif
#endif
#ifndef upg_oreCostFactor_g
#ifdef OC_DLL_MODE
#define upg_oreCostFactor_g  (reinterpret_cast<WORD *>(OC_VA(0x524950u)))
#else
extern WORD upg_oreCostFactor_g[61];
#endif
#endif
#ifndef upg_gasCostBase_g
#ifdef OC_DLL_MODE
#define upg_gasCostBase_g    (reinterpret_cast<WORD *>(OC_VA(0x5249D0u)))
#else
extern WORD upg_gasCostBase_g[61];
#endif
#endif
#ifndef upg_gasCostFactor_g
#ifdef OC_DLL_MODE
#define upg_gasCostFactor_g  (reinterpret_cast<WORD *>(OC_VA(0x524A50u)))
#else
extern WORD upg_gasCostFactor_g[61];
#endif
#endif
#ifndef upg_timeCostBase_g
#ifdef OC_DLL_MODE
#define upg_timeCostBase_g   (reinterpret_cast<WORD *>(OC_VA(0x524AD0u)))
#else
extern WORD upg_timeCostBase_g[61];
#endif
#endif
#ifndef upg_timeCostFactor_g
#ifdef OC_DLL_MODE
#define upg_timeCostFactor_g (reinterpret_cast<WORD *>(OC_VA(0x524B50u)))
#else
extern WORD upg_timeCostFactor_g[61];
#endif
#endif
#ifndef techdata_oreCost_g
#ifdef OC_DLL_MODE
#define techdata_oreCost_g   (reinterpret_cast<WORD *>(OC_VA(0x51F760u)))
#else
extern WORD techdata_oreCost_g[44];
#endif
#endif
#ifndef techdata_gasCost_g
#ifdef OC_DLL_MODE
#define techdata_gasCost_g   (reinterpret_cast<WORD *>(OC_VA(0x51F7B8u)))
#else
extern WORD techdata_gasCost_g[44] OC_UNALIGNED;
#endif
#endif
#ifndef techdata_timeCost_g
#ifdef OC_DLL_MODE
#define techdata_timeCost_g  (reinterpret_cast<WORD *>(OC_VA(0x51F810u)))
#else
extern WORD techdata_timeCost_g[44];
#endif
#endif
#ifndef gubUnitArmorType
#ifdef OC_DLL_MODE
#define gubUnitArmorType     (reinterpret_cast<BYTE *>(OC_VA(0x521BC0u)))
#else
extern upgrade_id_t gubUnitArmorType[228];
#endif
#endif
#ifndef gubFlingyMoveType
#ifdef OC_DLL_MODE
#define gubFlingyMoveType    (reinterpret_cast<BYTE *>(OC_VA(0x511670u)))
#else
extern BYTE gubFlingyMoveType[209] OC_UNALIGNED;
#endif
#endif

// CUnit::ApplyUpgrades -- re-apply every movement and speed upgrade this unit's type is
// affected by, then refresh the cached movement parameters. The switch is on wType and
// the default returns without the refresh: an unlisted unit type does nothing at all, and
// only a `break` out of a case reaches it. Case order is 1.17's, not sorted by value.
void CUnit::ApplyUpgrades() {
  switch (this->wType) {
    case 0x02:
    case 0x13:
      if (get_upgrade_level(this->bPlayer, 0x11))
        this->SetMovementUpgrade();
      break;
    case 0x2A:
      if (get_upgrade_level(this->bPlayer, 0x1A))
        this->SetMovementUpgrade();
      break;
    case 0x25:
      if (get_upgrade_level(this->bPlayer, 0x1B))
        this->SetMovementUpgrade();
      if (get_upgrade_level(this->bPlayer, 0x1C))
        this->dwFlags |= 0x20000000;
      break;
    case 0x36:
      this->SetMovementUpgrade();
      this->dwFlags |= 0x20000000;
      break;
    case 0x26:
      if (get_upgrade_level(this->bPlayer, 0x1D))
        this->SetMovementUpgrade();
      break;
    case 0x41:
      if (get_upgrade_level(this->bPlayer, 0x22))
        this->SetMovementUpgrade();
      break;
    case 0x46:
      if (get_upgrade_level(this->bPlayer, 0x2A))
        this->SetMovementUpgrade();
      break;
    case 0x45:
      if (get_upgrade_level(this->bPlayer, 0x25))
        this->SetMovementUpgrade();
      break;
    case 0x54:
      if (get_upgrade_level(this->bPlayer, 0x27))
        this->SetMovementUpgrade();
      break;
    case 0x27:
      if (get_upgrade_level(this->bPlayer, 0x35))
        this->SetMovementUpgrade();
      break;
    case 0x30:
      if (gbIsExpansion != 0)
        this->SetMovementUpgrade();
      break;
    case 0x35:
    case 0x39:
    case 0x4D:
    case 0x50:
    case 0x58:
    case 0x67:
      this->SetMovementUpgrade();
      break;
    default:
      return;
  }
  this->ResetMovementSpeed();
}

// CUnit::ArmorUpgrades (0x43AB10, 104 B) -- this unit's armor upgrade level.
//
// The expansion gate is the fast path and it is a tail jmp: the fall-through ends
// `pop esi / jmp get_upgrade_level`, so vanilla returns the plain lookup and only Brood
// War runs the Ultralisk carapace bonus. The return is a BYTE even though the slow path's
// add is 32-bit: with a BYTE return the upper half of EAX is dead, so MSVC neither narrows
// the add nor widens the call result, and those two skipped widenings are the 7 bytes
// between the int form and this one. All three callers consume EAX 8-bit.
BYTE CUnit::ArmorUpgrades() {
  WORD wType = this->wType;
  if (gbIsExpansion == 0)
    return get_upgrade_level(this->bPlayer, gubUnitArmorType[wType]);

  BYTE bBonus = 0;
  if (wType == 0x27 || wType == 0x30) {
    if ((gulUnitTypeFlags[wType] & 0x40)
        || get_upgrade_level(this->bPlayer, 0x34))
      bBonus = 2;
  }
  return get_upgrade_level(this->bPlayer, gubUnitArmorType[this->wType]) + bBonus;
}

// CUnit::CancelResearch (0x43AB80, 96 B) -- the player cancelled a tech that had already
// started: credit the whole techdata.dat cost back, clear the in-progress button state and
// the countdown, and put the building's sprite back on its idle animation.
//
// One local for the tech id, but only up to the call: the oracle loads `mov cl, [esi+0C8h]`
// once for both the guard and the subscript, then re-reads the field after
// clear_tech_in_progress -- a local promises the value is the same on both sides of a
// call, a field read promises it might not be. The player is the other way round and must
// be cached: spelled `gPlayerMinerals[this->bPlayer] += ...` twice, MSVC emits two reads
// and spends a second callee-saved register on the tech index.
void CUnit::CancelResearch() {
  tech_id_t bTech = this->bTechType;
  if (bTech == 0x2C)
    return;
  BYTE bPlayer = this->bPlayer;
  gPlayerMinerals[bPlayer] += techdata_oreCost_g[bTech];
  gPlayerGas[bPlayer] += techdata_gasCost_g[bTech];
  clear_tech_in_progress(this->bTechType, this->bPlayer);
  this->bTechType = 0x2C;
  this->wRemainingResearchTime = 0;
  this->pSprite->SetAnimation(0x14, 1);
}

// CUnit::CancelUpgrade (0x43ABE0, 87 B) -- the upgrade twin, with two guards. The outer one
// is `test byte ptr [eax*4+521660h], 1h` on gulUnitTypeFlags indexed by wType, and both
// jzs land on the same `pop esi` at offset 85, so they are two peer early returns and not
// a nesting.
void CUnit::CancelUpgrade() {
  if (!(gulUnitTypeFlags[this->wType] & 1))
    return;
  upgrade_id_t bUpgrade = this->bUpgradeType;
  if (bUpgrade == 0x3D)
    return;
  refund_upgrade_full(this->bPlayer, bUpgrade);
  clear_upgrade_in_progress(this->bUpgradeType, this->bPlayer);
  this->bUpgradeType = 0x3D;
  this->wRemainingResearchTime = 0;
  this->bTargetUpgradeLevel = 0;
  this->pSprite->SetAnimation(0x14, 1);
}

// CUnit::DispatchResearchTech (0x43AC40, 154 B) -- one tick of a research building's
// countdown, and the completion path when it reaches zero.
//
// The three conditions are one short-circuit chain whose false arm returns and whose true
// arm falls into the completion block: only the `test [5432FCh], 2` guard is a real early
// return, and the "no tech in progress" test skips straight to the teardown. The countdown
// tests the old value -- `movzx edx, word ptr [esi+0C6h] / lea eax, [edx-1h] / mov word
// ptr [esi+0C6h], ax / test dx, dx` is a post-decrement, and testing the stored value is a
// same-size wrong program.
void CUnit::DispatchResearchTech() {
  tech_id_t bTech = this->bTechType;
  if (bTech != 0x2C) {
    if (this->wRemainingResearchTime-- != 0
        && !get_tech_researched(this->bPlayer, bTech)
        && !(glCheatBits & 2))
      return;
    gamesnd_research_complete(this);
    set_tech_researched(this->bPlayer, this->bTechType, 1);
    status_set_update();
  }
  this->pSprite->SetAnimation(0x14, 1);
  clear_tech_in_progress(this->bTechType, this->bPlayer);
  this->bTechType = 0x2C;
  if (this->dwUnion134 != 0)
    this->SetOrder(0x9C);
  else
    this->EndDispatchOrder();
}

// Counts an upgrade down and, when it completes, applies it and tears down the dispatch.
// Both top guards jump to the shared tear-down, and the countdown tests the value before
// the decrement. The player goes to get_max_upgrade_level as a WORD, which keeps it in BH.
void CUnit::DispatchUpgrade() {
  if ((this->dwFlags & 2) != 0) {
    upgrade_id_t bUpgrade = this->bUpgradeType;
    if (bUpgrade != 0x3D) {
      WORD wTime = this->wRemainingResearchTime;
      this->wRemainingResearchTime = (WORD)(wTime - 1);
      if (wTime != 0
          && get_upgrade_level(this->bPlayer, bUpgrade) < this->bTargetUpgradeLevel
          && (glCheatBits & 2) == 0)
        return;

      gamesnd_upgrade_finished(this);
      if (get_upgrade_level(this->bPlayer, this->bUpgradeType)
            < this->bTargetUpgradeLevel) {
        BYTE bPlayer = this->bPlayer;
        int nType = this->bUpgradeType;
        BYTE bMax = get_max_upgrade_level((WORD)bPlayer, nType);
        if (get_upgrade_level(bPlayer, nType) < bMax) {
          set_upgrade_level(this->bPlayer, this->bUpgradeType,
                            this->bTargetUpgradeLevel);
          this->sub_43B0B0(this->bUpgradeType);
        }
      }
      status_set_update();
    }
  }
  this->pSprite->SetAnimation(0x14, 1);
  clear_upgrade_in_progress(this->bUpgradeType, this->bPlayer);
  this->bUpgradeType = 0x3D;
  this->bTargetUpgradeLevel = 0;
  if (this->dwUnion134 != 0) {
    this->SetOrder(0x9C);
    return;
  }
  this->EndDispatchOrder();
}

// CUnit::RefundResearch (0x43ADF0, 110 B) -- CancelResearch's rate-reduced sibling: three
// quarters of the cost back, and no animation change.
//
// The arithmetic is written out literally rather than routed through a helper, as
// refund_upgrade_partial below is: the oracle emits MSVC's signed `x * 3 / 4` sequence
// (`lea eax, [eax+eax*2] / cdq / and edx, 3h / add eax, edx / sar eax, 2h`) inline twice.
// The multiplicand is a zero-extended WORD, so the product cannot reach the sign bit and
// the signed form is the compiler's choice of division sequence.
//
// The player local is declared before the tech local, and that is the whole shape: both
// are used twice, so the spill weights tie and MSVC falls back to definition order for the
// one callee-saved register the cdq pressure leaves. With bTech first -- the spelling that
// matches CancelResearch -- this body is the same program with ESI and ECX swapped.
// CancelResearch wants the opposite order, having no cdq pressure and so no callee-saved
// register to rank for.
void CUnit::RefundResearch() {
  if (this->bTechType == 0x2C)
    return;
  BYTE bPlayer = this->bPlayer;
  tech_id_t bTech = this->bTechType;
  int nOre = techdata_oreCost_g[bTech];
  gPlayerMinerals[bPlayer] += nOre * 3 / 4;
  int nGas = techdata_gasCost_g[bTech];
  gPlayerGas[bPlayer] += nGas * 3 / 4;
  clear_tech_in_progress(this->bTechType, this->bPlayer);
  this->bTechType = 0x2C;
  this->wRemainingResearchTime = 0;
}

// CUnit::RefundUpgrade (0x43AE60, 61 B) -- CancelUpgrade without the
// gulUnitTypeFlags guard and without the animation, calling the partial refund.
void CUnit::RefundUpgrade() {
  upgrade_id_t bUpgrade = this->bUpgradeType;
  if (bUpgrade == 0x3D)
    return;
  refund_upgrade_partial(this->bPlayer, bUpgrade);
  clear_upgrade_in_progress(this->bUpgradeType, this->bPlayer);
  this->bUpgradeType = 0x3D;
  this->wRemainingResearchTime = 0;
  this->bTargetUpgradeLevel = 0;
}

// CUnit::ResetMovementSpeed (0x43AEA0, 65 B) -- re-derive the three cached movement
// parameters from the unit's current upgrade state. The guard is the fall-through and not
// the then-arm: both `jz` land past the tail jmp, so `jmp sub_42DFF0` is what runs when
// the move type is neither 0 nor 1.
void CUnit::ResetMovementSpeed() {
  BYTE bMoveType = gubFlingyMoveType[gubUnitFlingy[this->wType]];
  if (bMoveType != 0 && bMoveType != 1) {
    this->sub_42DFF0();
    return;
  }
  this->lTopSpeed = this->FlingyMaxSpeedWithModifiers();
  this->wAcceleration = this->FlingyAccelWithModifiers();
  this->bTurnRadius = this->FlingyTurnRadiusWithModifiers();
}

// CUnit::SetMovementUpgrade (0x43AEF0, 75 B) -- ResetMovementSpeed with the
// "speed upgraded" flag set first. 75 == 65 + 10, and 10 is exactly the
// encoding length of `or dword ptr [esi+0DCh], 10000000h` (81 /1 + disp32 +
// imm32), so the whole difference is that one instruction and the rest is the
// callee spliced in by /Ob2.
void CUnit::SetMovementUpgrade() {
  this->dwFlags |= 0x10000000;
  this->ResetMovementSpeed();
}

// CUnit::StartResearch (0x43AF40, 105 B) -- begin researching a tech at this building, if
// the player can pay for it.
//
// The two failure paths are cross-jumped onto one epilogue, which comes of writing two
// separate `return FALSE;` guards: MSVC places the FALSE block after the first guard and
// reaches it backward from the cost check, where one || chain would put it after both
// tests. The parameter is a BYTE: the oracle opens `mov bl, byte ptr [ebp+8h]`, and an int
// parameter opens `mov ebx, dword ptr [ebp+8h]` whatever cast is applied to it, because
// the cast is a no-op subregister view and MSVC keeps the slot's declared width.
BOOL CUnit::StartResearch(tech_id_t bTech) {
  if (!TechTypeValid(bTech))  // Hyperion: and a mod's past 1.17's rows
    return FALSE;
  if (!bldg_get_tech_cost(this->bPlayer, bTech, 1))
    return FALSE;
  this->bTechType = bTech;
  this->wRemainingResearchTime = techdata_timeCost_g[bTech];
  bldg_deduct_resources(this->bPlayer);
  set_tech_in_progress(bTech, this->bPlayer);
  this->pSprite->SetAnimation(0x13, 1);
  return TRUE;
}

// CUnit::StartUpgrade (0x43AFB0, 165 B) -- the upgrade arm of the same shape, plus the two
// get_upgrade_level queries.
//
// The time cost is written out literally and is not a call to get_upgrade_time (0x43B350),
// which is landed in this very TU: the oracle here is 16-bit
// (`imul ax, word ptr [esi*2+524B50h]`) where that function is 32-bit. The frame and the
// `this` spill are register pressure and not source: ESI is clobbered with the table index
// and `this` reloaded from [ebp-4h] afterwards.
//
// The first player read is source, though. The oracle reads the player into AL while ESI
// is still `this`, so bPlayer has to be bound to a local before the expression; spelled
// `get_upgrade_level(this->bPlayer, ..)` MSVC reloads `this` from the spill slot instead,
// one byte longer, and the whole tail drifts by that byte. The three later player reads
// must stay field re-reads, each one after a call. The parameter is a BYTE for the same
// reason as StartResearch above.
BOOL CUnit::StartUpgrade(upgrade_id_t bUpgrade) {
  if (!UpgradeTypeValid(bUpgrade))  // Hyperion: and a mod's past 1.17's rows
    return FALSE;
  if (!bldg_get_upgrade_cost(this->bPlayer, bUpgrade, 1))
    return FALSE;
  BYTE bPlayer = this->bPlayer;
  this->bUpgradeType = bUpgrade;
  this->wRemainingResearchTime =
      (WORD)(upg_timeCostFactor_g[bUpgrade]
                 * get_upgrade_level(bPlayer, bUpgrade)
             + upg_timeCostBase_g[bUpgrade]);
  this->bTargetUpgradeLevel =
      (BYTE)(get_upgrade_level(this->bPlayer, bUpgrade) + 1);
  bldg_deduct_resources(this->bPlayer);
  set_upgrade_in_progress(bUpgrade, this->bPlayer);
  this->pSprite->SetAnimation(0x13, 1);
  return TRUE;
}

// unit_is_type_cb (0x43B060, 12 B) -- the generic arm of the three predicates
// sub_43B0B0 dispatches through. `xor eax, eax` before the `cmp` and `setz al`
// after it is MSVC's standard BOOL-from-compare, so the source is the bare
// comparison and not an if/else.
BOOL unit_is_type_cb(CUnit *pUnit, int nType) {
  return pUnit->wType == nType;
}

// unit_is_vulture_cb (0x43B070, 23 B) -- Vulture (0x02) or the hero Vulture
// (0x13). Two `jz` into a shared `mov eax, 1h` with `xor eax, eax` as the
// fall-through is the branchy form, not the setcc form. The second parameter is
// unread here -- see CUnitUpgrade.h on why it is declared anyway.
BOOL unit_is_vulture_cb(CUnit *pUnit, int) {
  WORD wType = pUnit->wType;
  if (wType == 0x02 || wType == 0x13)
    return TRUE;
  return FALSE;
}

// unit_is_ultralisk_cb (0x43B090, 23 B) -- byte-twin of the Vulture predicate
// with Ultralisk (0x27) and Torrasque (0x30).
BOOL unit_is_ultralisk_cb(CUnit *pUnit, int) {
  WORD wType = pUnit->wType;
  if (wType == 0x27 || wType == 0x30)
    return TRUE;
  return FALSE;
}

// CUnit::sub_43B0B0 -- an upgrade just completed for this unit's owner; re-walk that
// player's unit list and refresh every unit the upgrade actually affects. It needs
// __declspec(noinline) on CUnit::SetMovementUpgrade, which /Ob2 would otherwise splice
// into this loop. The null test on the predicate stays though every switch arm sets one:
// the two ways out of range skip the switch. All three callbacks take two parameters.
void CUnit::sub_43B0B0(int a1) {
  BOOL (*pfn)(CUnit *pUnit, int nType) = NULL;
  WORD wType = 0;
  BYTE bSetTypeFlag = 0;
  BYTE bSetSpeed = 1;

  switch ((BYTE)a1) {
    case 0x11: pfn = unit_is_vulture_cb;   wType = 0x02; break;
    case 0x1A: pfn = unit_is_type_cb;      wType = 0x2A; break;
    case 0x1B: pfn = unit_is_type_cb;      wType = 0x25; break;
    case 0x1C: pfn = unit_is_type_cb;      bSetTypeFlag = 1; wType = 0x25;
               bSetSpeed = 0; break;
    case 0x1D: pfn = unit_is_type_cb;      wType = 0x26; break;
    case 0x22: pfn = unit_is_type_cb;      wType = 0x41; break;
    case 0x2A: pfn = unit_is_type_cb;      wType = 0x46; break;
    case 0x25: pfn = unit_is_type_cb;      wType = 0x45; break;
    case 0x27: pfn = unit_is_type_cb;      wType = 0x54; break;
    case 0x35: pfn = unit_is_ultralisk_cb; wType = 0x27; break;
  }
  if (pfn == NULL)
    return;

  CUnit *pUnit = CUnit::GetPlayerUnitList(this->bPlayer);
  while (pUnit != NULL) {
    if (pfn(pUnit, wType)) {
      if (bSetTypeFlag)
        pUnit->dwFlags |= 0x20000000;
      if (bSetSpeed)
        pUnit->SetMovementUpgrade();
    }
    pUnit = pUnit->pPlayerUnitNext;
  }
}

// refund_upgrade_full (0x43B210, 94 B) -- credit an upgrade's whole ore and gas cost back
// to the player who cancelled it. The arithmetic is written out literally, and that is the
// derivation: get_upgrade_ore_cost below computes the same quantity with a 32-bit
// `imul eax, ecx`, while this body has `imul ax, word ptr [edi*2+524950h]` and a 16-bit
// add, so a call that /Ob2 spliced would have brought the wider form with it.
void refund_upgrade_full(BYTE bPlayer, upgrade_id_t bUpgradeId) {
  WORD wOre = (WORD)(upg_oreCostFactor_g[bUpgradeId]
                       * get_upgrade_level(bPlayer, bUpgradeId)
                     + upg_oreCostBase_g[bUpgradeId]);
  gPlayerMinerals[bPlayer] += wOre;
  WORD wGas = (WORD)(upg_gasCostFactor_g[bUpgradeId]
                       * get_upgrade_level(bPlayer, bUpgradeId)
                     + upg_gasCostBase_g[bUpgradeId]);
  gPlayerGas[bPlayer] += wGas;
}

// get_upgrade_gas_cost (0x43B270, 41 B) -- see src117/h/CUnitUpgrade.h for the
// signature derivation. base + factor * level, all 32-bit.
WORD get_upgrade_gas_cost(BYTE bPlayer, upgrade_id_t bUpgradeId) {
  return upg_gasCostFactor_g[bUpgradeId] * get_upgrade_level(bPlayer, bUpgradeId)
         + upg_gasCostBase_g[bUpgradeId];
}

// get_upgrade_ore_cost (0x43B2A0, 41 B) -- byte-twin of the gas query.
WORD get_upgrade_ore_cost(BYTE bPlayer, upgrade_id_t bUpgradeId) {
  return upg_oreCostFactor_g[bUpgradeId] * get_upgrade_level(bPlayer, bUpgradeId)
         + upg_oreCostBase_g[bUpgradeId];
}

// refund_upgrade_partial (0x43B2D0, 118 B) -- three quarters of the cost, the
// cancel-after-start rate. `lea eax, [eax+eax*2] / cdq / and edx, 3h / add eax,
// edx / sar eax, 2h` is MSVC's signed `x * 3 / 4`; the operand is the
// zero-extended 16-bit cost, so the multiply cannot reach the sign bit and the
// signed form is the compiler's, not a claim about the data.
void refund_upgrade_partial(BYTE bPlayer, upgrade_id_t bUpgradeId) {
  int nOre = (WORD)(upg_oreCostFactor_g[bUpgradeId]
                      * get_upgrade_level(bPlayer, bUpgradeId)
                    + upg_oreCostBase_g[bUpgradeId]);
  gPlayerMinerals[bPlayer] += nOre * 3 / 4;
  int nGas = (WORD)(upg_gasCostFactor_g[bUpgradeId]
                      * get_upgrade_level(bPlayer, bUpgradeId)
                    + upg_gasCostBase_g[bUpgradeId]);
  gPlayerGas[bPlayer] += nGas * 3 / 4;
}

// get_upgrade_time (0x43B350, 41 B) -- byte-twin of the gas query against the
// time-cost pair.
WORD get_upgrade_time(BYTE bPlayer, upgrade_id_t bUpgradeId) {
  return upg_timeCostFactor_g[bUpgradeId] * get_upgrade_level(bPlayer, bUpgradeId)
         + upg_timeCostBase_g[bUpgradeId];
}
