// SAI_Main.cpp -- the AI module's spine: init, update and dispatch, in oracle
// VA order.

#include "oc_data.h"  // Hyperion: OC_VA
#include "../h/SAI_Main.h"
#include "../h/sai.h"
#include "../h/SAI_Banker.h"
#include "../h/SAI_Build.h"
#include "../h/SAI_Scripts.h"
#include "../h/sai_TownBuilder.h"
#include "../h/TAI_Captain.h"
#include "../h/CUnit.h"
#include "../h/CUnitInit.h"
#include "../h/CUnitSearches.h"
#include "../h/CUnitFind.h"
#include "../h/CUnitCombat.h"
#include "../h/CUnitMisc.h"
#include "../h/CUnitZSpells.h"
#include "../h/CSprite.h"
#include "../h/CTarget.h"
#include "../h/rect16.h"
#include "../h/rect32.h"
#include "../h/statbtn.h"
#include "../h/order.h"
#include "../h/TrigAct.h"
#include "../h/placebox.h"
#include "../h/net_cmds.h"
#include "../h/TAI_GuardPost.h"
#include "../h/gamesnd.h"

#include "../gen/globals117.h"
#include "../h/typelimits.h"         // Hyperion: UnitTypeMod
#include "../port/data/port_moddata.h"    // Hyperion: ModCardButtons
#include "../port/gameplay/port_ai.h"         // Hyperion: AiThinkFrames

// Every callee this TU reaches is declared in a header, none of them here:
// tai_captain_init, TAI_Captain_RemoveUnit and tai_review_allied_strength in
// TAI_Captain.h, tai_guardpost_init and TAI_GuardPost_RemoveUnit in
// TAI_GuardPost.h, bldg_hand_over_product and town_expand_tick in SAI_Build.h,
// gamesnd_rescue in gamesnd.h. A second declaration here would make the
// generator emit nothing for that name.
//
// CUnit::DispatchRescuePassive and CUnit::DispatchComputerAI_Bunker are members
// declared in src117/h/CUnit.h, both nullary. `this` and a single CUnit*
// parameter both arrive in ECX under /Gr, so only the spelling moved.

// The globals this TU reaches, dual-spelled with one guard per name. The four
// dword_69B5xx names are the naming sheet's own, and it seats all four in this
// TU; none of them is registered for generation.
//
// They are four separate dword scalars, not an array: sai_init and
// ai_periodic_timer_tick bake 0x69B564 / 0x69B568 / 0x69B56C as direct dword
// literals (`mov dword ptr [69B564h], 3Ch`), never through an index. Every
// reference is 32-bit at all ten sites.

// Their roles, from those sites: 0x69B560 is a sticky one-shot flag (sai_init
// zeroes it, ai_mark_targets_stale sets it, ai_mark_targets_stale_on_death
// cmov-sets it, ai_periodic_timer_tick tests and clears it), and the other three
// are down-counters seeded 0x3C / 0x0F / 0x06 and reloaded 0x25-or-0x07 /
// 0x12C / 0x06.

// gubUnitOrderAI -- BYTE[228] @0x520D50 (arr/units.cpp in the naming sheet,
// beside the registered gubUnitOrderGuard @0x520F20 and
// gubUnitOrderAttackUnit @0x521008). Indexed by CUnit::wType with the same
// `movzx eax, byte ptr [eax+<base>]` shape as its two neighbours.
#ifndef gubUnitOrderAI
#ifdef OC_DLL_MODE
#define gubUnitOrderAI  (reinterpret_cast<BYTE *>(OC_VA(0x520D50u)))
#else
extern order_id_t gubUnitOrderAI[228] OC_UNALIGNED;
#endif
#endif

#ifndef dword_69B560
#ifdef OC_DLL_MODE
#define dword_69B560    (*reinterpret_cast<DWORD *>(OC_VA(0x69B560u)))
#else
extern DWORD dword_69B560;
#endif
#endif

#ifndef dword_69B564
#ifdef OC_DLL_MODE
#define dword_69B564    (*reinterpret_cast<DWORD *>(OC_VA(0x69B564u)))
#else
extern DWORD dword_69B564;
#endif
#endif

#ifndef dword_69B568
#ifdef OC_DLL_MODE
#define dword_69B568    (*reinterpret_cast<DWORD *>(OC_VA(0x69B568u)))
#else
extern DWORD dword_69B568;
#endif
#endif

#ifndef dword_69B56C
#ifdef OC_DLL_MODE
#define dword_69B56C    (*reinterpret_cast<DWORD *>(OC_VA(0x69B56Cu)))
#else
extern DWORD dword_69B56C;
#endif
#endif

// The bodies, in oracle VA order.

// CUnit::DispatchComputerAI -- oracle 0x48CD70 (364 B). The computer player's
// per-unit tick: the two zerg cases, the AI record's own type tag, then the
// Terran repair/spell arm and the two burrow helpers.
//
// The 45-or-3 reload is a ternary of two constants: `neg / sbb al, al /
// and al, 2Ah / add al, 3`, 8-bit because the destination is a BYTE field.
void CUnit::DispatchComputerAI()
{
  if (this->bUnion54 != 0)
    return;
  this->bUnion54 = AiThinkFrames(this, 0x0F);  // Hyperion: 0x0F, a computer's profile may change it
  if (this->wType == 0x23) {
    this->DispatchIdleLarva();
    if (this->IsDeadOrDying())
      return;
  } else if (this->wType == 0x22) {
    if (tai_soldier_captain_is_idle(this)) {
      AIMoveWhereNeeded(this);
      return;
    }
    this->SetOrder(0xAF);
    return;
  }
  if (this->dwUnion134 == 0) {
    this->SetOrder(gubUnitOrderAI[this->wType]);
    return;
  }
  if (ai_update_unit_idle(this))
    return;

  {
    BYTE bType = reinterpret_cast<TWorker *>(this->dwUnion134)->bType;

    if (bType == 2) {
      worker_tick(this);
    } else if (bType == 3) {
      this->bUnion54 = AiThinkFrames(this, static_cast<BYTE>(bldg_tick(this) ? 45 : 3));  // Hyperion
    } else if (bType == 4) {
      tai_soldier_update(this);
      this->bUnion54 = 3;
    } else {
      this->SetOrder(gubUnitOrderAI[this->wType]);
    }
  }

  if (this->GetRace() == 1
      && (gulUnitTypeFlags[this->wType] & 0x40000000) != 0
      && !tai_soldier_captain_is_attacking(this, 0)
      && ((this->lHitPoints + 0xFF) >> 8) != this->GetLife()
      && (this->dwFlags & 0x20000) != 0
      && this->bBloodBoilTimer == 0
      && this->bIrradiateTimer == 0
      && (gulUnitTypeFlags[this->wType] & 8) == 0) {
    CUnit *pTarget = this->sub_432BB0();

    if (pTarget == NULL)
      return;
    this->SetOrder_CUnit(0x31, pTarget);
    return;
  }
  if (this->wType == 5) {
    ai_try_siege(reinterpret_cast<intptr_t>(this));
    return;
  }
  if ((gulUnitTypeFlags[this->wType] & 0x100000) != 0)
    ai_try_burrow(reinterpret_cast<intptr_t>(this));
}

// CUnit::DispatchAIPatrol -- oracle 0x48CEE0 (207 B). A three-state patrol:
// walk to the target, wait for the captain, walk back. bUnion54 is stamped
// 0x0F before the switch, so a failed arm still costs the same wait.
//
// bOrderState = 0 in the wait arm is the oracle's `mov [esi+4Eh], al` with AL
// zero from the test above it; a literal 0 in the source gets the same reuse.
void CUnit::DispatchAIPatrol()
{
  int nX;
  int nY;

  if (this->bUnion54 != 0)
    return;
  this->bUnion54 = 0x0F;
  switch (this->bOrderState) {
  case 0:
    if (this->pOrderTarget != NULL) {
      this->wOrderTargetX = this->pOrderTarget->pSprite->wPosX;
      this->wOrderTargetY = this->pOrderTarget->pSprite->wPosY;
      this->SetDestination_xy(this->wOrderTargetX, this->wOrderTargetY);
      return;
    }
    if (patroller_follow_captain(this))
      break;
    if (tai_captain_take_patroller(this))
      break;
    this->bOrderState = 2;
    return;
  case 1:
    if (patroller_follow_captain(this))
      return;
    this->bOrderState = 0;
    return;
  case 2:
    if (tai_captain_take_patroller(this))
      break;
    if (!this->AtMoveTarget())
      return;
    patrol_pick_area(this, &nX, &nY);
    this->SetDestination_xy(nX, nY);
    return;
  default:
    return;
  }
  this->bOrderState = 1;
}

// CUnit::DispatchNeutral -- oracle 0x48CFB0 (364 B). A neutral player that has
// been attacked turns hostile: its slot becomes a computer, both alliance
// cells are cleared, and every unit it owns is sent after the attacker.
//
// The guards read the attacker through a local and everything after them
// re-reads this->dwUnion7C; the oracle loads [edi+7Ch] again after the call.
void CUnit::DispatchNeutral()
{
  CUnit *pAttacker = reinterpret_cast<CUnit *>(this->dwUnion7C);
  CUnit *pUnit;

  if (pAttacker == NULL)
    return;
  if ((pAttacker->pSprite->flags.b & 0x20) == 0
      && !pAttacker->IsVisibleTo(this)
      && this->bPlayer != pAttacker->bPlayer) {
    CUnit *pSubunit;

    gPlayerData[this->bPlayer].nType = 1;
    sgbAlliances_g[this->bPlayer]
                  [reinterpret_cast<CUnit *>(this->dwUnion7C)->bPlayer] = 0;
    sgbAlliances_g[reinterpret_cast<CUnit *>(this->dwUnion7C)->bPlayer]
                  [this->bPlayer] = 0;
    if (this->CanTarget(reinterpret_cast<CUnit *>(this->dwUnion7C), 1)) {
      this->ClearTarget();
      this->SetOrder_CUnit(gubUnitOrderAttackUnit[this->wType],
                           reinterpret_cast<CUnit *>(this->dwUnion7C));
      pSubunit = this->subunit;
      if (this->IsTurret(pSubunit))
        pSubunit->SetOrder_CUnit(gubUnitOrderAttackUnit[pSubunit->wType],
                                 reinterpret_cast<CUnit *>(this->dwUnion7C));
    } else {
      this->SetOrder(gubUnitOrderGuard[this->wType]);
    }
    this->AttachAiRecord();

    for (pUnit = CUnit::GetPlayerUnitList(this->bPlayer); pUnit != NULL;
         pUnit = pUnit->pPlayerUnitNext) {
      CUnit *pTurret;
      DWORD dwFlags;

      if (pUnit->IsDeadOrDying())
        continue;
      if ((gulUnitTypeFlags[pUnit->wType] & 0x10) != 0)
        continue;
      if (pUnit == this)
        continue;
      dwFlags = pUnit->dwFlags;
      if ((dwFlags & 0x40) != 0) {
        if ((dwFlags & 0x20) != 0)
          pUnit->SetOrder(5);
      } else {
        pUnit->SetOrder(gubUnitOrderGuard[pUnit->wType]);
      }
      pTurret = pUnit->subunit;
      if (pUnit->IsTurret(pTurret))
        pTurret->SetOrder_CUnit(gubUnitOrderAttackUnit[pTurret->wType],
                                reinterpret_cast<CUnit *>(this->dwUnion7C));
      pUnit->AttachAiRecord();
    }
  }
  this->dwUnion7C = 0;
}

// DispatchRescuePassive -- oracle 0x48D120 (423 B).
//
// A rescue-passive unit looking for a rescuer. It scans a 0x80-wide box around
// itself for a unit owned by a player of type 2; on the first hit, either the
// whole owning nation's unit list is re-ordered (the handover arm) or just this
// unit is (the self arm), then the rescue sound and a status refresh.

// The eligibility test is a disjunction with a guarded fourth arm: reaching the
// handover arm needs (dwFlags & 1) or wType == 0x85 or 0x84 or (wType == 0x83
// and IsZergBldgUpgrading()), and anything else falls to the self arm. It is
// read off the branch targets rather than the mnemonics -- the `jnz` at +0xC2
// is the else of the 0x83 arm, and the IsZergBldgUpgrading call falls through.
// The early exit on bUnion54 returns before ESI and EDI are pushed, so it pops
// only EBX.
//
// finish_unit_iteration is emitted twice because the found path re-loads ppList
// from [ebp-4] -- ESI is reused for the player-list walk -- while the
// scan-exhausted path still has it live. That duplication is what a `return`
// inside the loop gives; a `break` would merge them into one tail.

// The rect is built left/right/top/bottom (displacements -0x10, -0x0C, -0x0E,
// -0x0A), not the top/bottom/left/right of the TAI_Captain family.
void CUnit::DispatchRescuePassive()
{
  mrect_t  rc;
  CSprite *pSprite;
  CUnit **ppList;
  CUnit **pp;
  CUnit *pCand;

  if (this->bUnion54 != 0)
    return;

  pSprite = this->pSprite;
  this->bUnion54 = 1;
  rc.left = PosS(pSprite->wPosX - 0x40);
  rc.right = PosS(pSprite->wPosX + 0x40);
  rc.top = PosS(pSprite->wPosY - 0x40);
  rc.bottom = PosS(pSprite->wPosY + 0x40);

  pp = get_units_in_rect(&rc);
  ppList = pp;
  for (pCand = *pp; pCand != NULL; pCand = *++pp) {
    if (gPlayerData[pCand->bPlayer].nType == 2)
      break;
  }
  if (pCand != NULL) {
    if (((this->dwFlags & 1) != 0 || this->wType == 0x85 ||
         this->wType == 0x84 ||
         (this->wType == 0x83 && this->IsZergBldgUpgrading())) &&
        (this->dwFlags & 2) != 0 &&
        (gulUnitTypeFlags[this->wType] & 0x1000) != 0) {
      CUnit *pMember;
      CUnit *pNext;

      for (pMember = CUnit::GetPlayerUnitList(this->bPlayer); pMember != NULL;
           pMember = pNext) {
        int nOrder;

        pNext = pMember->pPlayerUnitNext;
        if (pMember->IsDeadOrDying())
          continue;
        pMember->SetPlayer(pCand->bPlayer, 1);
        if ((pMember->dwFlags & 0x20) != 0)
          nOrder = 5;
        else
          nOrder = gubUnitOrderInitialize[pMember->wType];
        pMember->SetOrder(nOrder);
      }
    } else {
      // This self arm duplicates the call where the handover arm above shares
      // it, and the asymmetry is the oracle's: the handover arm merges on one
      // `call CUnit::SetOrder` at 0x13E after a `jmp`, while this
      // arm emits a separate call in each branch (0x18B and 0x1A0), each
      // followed by its own `jmp` to the tail. Writing this one with a shared
      // `nOrder` local too costs ~11 B and shifts every following block.
      int nOrder;

      this->SetPlayer(pCand->bPlayer, 1);
      if ((this->dwFlags & 0x20) != 0)
        nOrder = 5;
      else
        nOrder = gubUnitOrderInitialize[this->wType];
      this->SetOrder(nOrder);
    }

    gamesnd_rescue(this);
    status_set_update();
  }
  finish_unit_iteration(ppList);
}

// DispatchComputerAI_Bunker -- oracle 0x48D2D0 (14 B).
//
//   test byte ptr [ecx+0DCh], 20h / jnz CUnit::DispatchGuardBunker / ret
//
// A conditional tail jump: the jnz is the call, so the if-body must be the last
// statement and must itself be a tail call. +0xDC is CUnit::dwFlags, and the
// mask is read `byte ptr` -- MSVC narrowing a low-byte mask on the dword field
// (the gulUnitTypeFlags precedent), not a byte member.
void CUnit::DispatchComputerAI_Bunker()
{
  if ((this->dwFlags & 0x20) != 0)
    this->DispatchGuardBunker();
}

// ai_target_unreachable -- oracle 0x48D2E0 (57 B).
//
// Three disqualifiers and one computed answer. The three `return 0` arms fold
// onto one shared `xor eax, eax / pop esi / ret` block, and that block is
// textually last in the oracle: all three branches run forward into it and the
// computed answer is the fall-through. Three early `return 0;` guards do not
// give that -- MSVC materialises the zero block at the first guard and the
// later two branch backward into it. Writing the conjunction as one guard
// around the computed return, with the bare `return 0;` last, is what puts the
// block at the end.

//   movzx eax, byte ptr [ecx+4Ch]      pA->bPlayer
//   lea   eax, [eax+eax*8]             *9, then *4 in the displacement => *36
//   cmp   byte ptr [eax*4+6726D0h], 1h gPlayerData[bPlayer].nType, +8 into a
//                                      36-byte record whose base is 0x6726C8
//   test  byte ptr [ecx+0DCh], 4h      pA->dwFlags & 4
//   push  esi / call CUnit::IsUnitInWeaponRange pA->IsUnitInWeaponRange(pB)   (ECX still pA)
//   mov   eax, dword ptr [esi+0DCh] / shr eax, 7h / and eax, 1h
BOOL __fastcall ai_target_unreachable(CUnit *pA, CUnit *pB)
{
  if (gPlayerData[pA->bPlayer].nType == 1 && (pA->dwFlags & 4) == 0 &&
      !pA->IsUnitInWeaponRange(pB))
    return (pB->dwFlags >> 7) & 1;
  return 0;
}

// register_unit_for_ai -- oracle 0x48D320 (90 B).
//
// A four-way dispatch on the parent's type, every arm a tail call taking the
// same (pUnit, pParent) pair: `mov ecx, edi` / `pop edi` / `pop esi` sits in
// front of each jmp, and EDX is loaded with pParent once before the first test
// and never reloaded, which is how all four arms are known to share it.
//
// The type is read once into ECX and the two literal compares reuse it
// (`cmp ecx, 2Ah` / `cmp ecx, 28h`), so wType is a named local, not three
// re-reads. The last two arms share one cmp + jz/jmp pair.
void __fastcall register_unit_for_ai(CUnit *pUnit, CUnit *pParent)
{
  WORD wType;

  if (gPlayerData[pUnit->bPlayer].nType != 1)
    return;

  banker_register_medic(pParent);
  wType = pParent->wType;
  if ((gulUnitTypeFlags[wType] & 8) != 0) {
    town_move_record(pUnit, pParent);
    return;
  }
  if (wType == 0x2A) {
    town_move_morphed_building(pUnit, pParent);
    return;
  }
  if (wType == 0x28) {
    tai_captain_enlist_as_attacker(pUnit, pParent);
    return;
  }
  bldg_hand_over_product(pUnit, pParent);
}

// sai_main_free -- oracle 0x48D380 (20 B). Frees the AI script and captain
// state. Both argument-free calls reach the folded one-byte `ret` at 0x4BE950,
// through gen's nullary alias of it.
void __fastcall voice_squelch();
void sai_main_free()
{
  voice_squelch();
  sai_scripts_free();
  voice_squelch();
  tai_captain_free();
}

// ai_update_unit_spells -- oracle 0x48D3A0 (1883 B).
// The unit's energy, in whole points, equals its maximum. The byte is read
// before the call, as the oracle does; written inline in the compare, MSVC
// reads it after the call and every site changes register.
static __forceinline BOOL ai_energy_is_full(CUnit *pUnit)
{
    BYTE bEnergy = (BYTE)(pUnit->wEnergy >> 8);
    return bEnergy == pUnit->GetMaxEnergyReal();
}

// Hyperion: a mod's unit's spells, as 1.17's are tried below. Each
// cast button of its card with an "ai" is tried in the card's order, its targets found by
// the finder the "ai" names. An aimed cast goes at the target; a cast where the caster
// stands is given when the target is inside its weapon's outer splash, which is where
// enemy_cluster searches for one.
static BOOL ai_update_mod_unit_spells(CUnit *pUnit, int nMode)
{
    static CUnit *(__fastcall *const kFinders[MOD_AI_FINDERS - 1])(CUnit *, int) = {
        AIFindPsiStormTarget, AIFindYamatoTarget, AIFindPlagueTarget, AIFindSwarmTarget,
        AIFindEnsnareTarget, AIFindBroodlingTarget, AIFindParasiteTarget, AIFindStasisTarget,
        AIFindEMPTarget, AIFindIrradiateTarget, AIFindMatrixTarget, AIFindDisruptorTarget,
        AIFindCureTarget, AIFindMyopiaTarget, AIFindPsyFeedbackTarget, AIFindMindControlTarget,
        AIFindParalyzeTarget, AIFindMagnapulseTarget, AIFindHallucinateTarget,
    };
    int nButtons = 0;
    const ModCardButton *pButtons = ModCardButtons(&nButtons);
    for (int k = 0; k < nButtons; k++)
    {
        const ModCardButton &b = pButtons[k];
        if (b.wCard != pUnit->wType || b.bAi == 0 || b.wUnit != 0xFFFF || b.bResearch || b.bUpgrade)
            continue;
        if (!ai_can_use_tech_order(pUnit, b.wOrder, b.wTech))
            continue;
        const weapon_id_t bWeapon = gubOrderWeapon[b.wOrder];
        CUnit *pTarget = b.bAi == MOD_AI_FINDERS
                             ? AIFindModClusterTarget(pUnit, nMode, bWeapon, b.bTarget ? 0 : wpn_outersplash_g[bWeapon])
                             : kFinders[b.bAi - 1](pUnit, nMode);
        if (pTarget == NULL)
            continue;
        if (!b.bTarget)
        {
            if (pUnit->GetDistanceTo_CUnit(pTarget) > wpn_outersplash_g[bWeapon])
                continue;
            pTarget = pUnit;
        }
        if (pUnit->bOrderId == b.wOrder && pUnit->pOrderTarget == pTarget)
            return FALSE;
        if (ai_unit_is_pursuing_order(pUnit, b.wOrder, pTarget, 1))
            return TRUE;
    }
    return FALSE;
}

// Each spell is tried as a block: already ordered at the same target returns
// FALSE, an issued order returns TRUE, anything else falls to the next spell.
// Source case order is the oracle's block order. Types are the switch base 1.
BOOL __fastcall ai_update_unit_spells(CUnit *pUnit, int nMode)
{
    CUnit *pTarget;

    if (nMode == 0 && !banker_ai_spells_allowed(pUnit->bPlayer))
        return FALSE;
    if (UnitTypeMod(pUnit->wType))  // Hyperion: a mod's unit
        return ai_update_mod_unit_spells(pUnit, nMode);

    switch (pUnit->wType)
    {
    case 0x0C:
        if (ai_can_use_tech_order(pUnit, 0x71, 8))
        {
            pTarget = AIFindYamatoTarget(pUnit, nMode);
            if (pUnit->bOrderId == 0x71 && pUnit->pOrderTarget == pTarget)
                return FALSE;
            if (ai_unit_is_pursuing_order(pUnit, 0x71, pTarget, 1))
                return TRUE;
        }
        break;

    case 0x2E:
        if (ai_can_use_tech_order(pUnit, 0x90, 0x0F))
        {
            pTarget = AIFindPlagueTarget(pUnit, nMode);
            if (pUnit->bOrderId == 0x90 && pUnit->pOrderTarget == pTarget)
                return FALSE;
            if (ai_unit_is_pursuing_order(pUnit, 0x90, pTarget, 1))
                return TRUE;
        }
        if (ai_can_use_tech_order(pUnit, 0x77, 0x0E))
        {
            pTarget = AIFindSwarmTarget(pUnit, nMode);
            if (pUnit->bOrderId == 0x77 && pUnit->pOrderTarget == pTarget)
                return FALSE;
            if (ai_unit_is_pursuing_order(pUnit, 0x77, pTarget, 1))
                return TRUE;
        }
        break;

    case 0x2D:
        if (aiscript_has_campaign(pUnit->bPlayer))
        {
            if ((BYTE)net_rand(0x2A) == 1)
                goto parasite;
        }
        else if (ai_energy_is_full(pUnit))
        {
            goto parasite;
        }
    broodling:
        if (ai_can_use_tech_order(pUnit, 0x79, 0x0D))
        {
            pTarget = AIFindBroodlingTarget(pUnit, nMode);
            if (pUnit->bOrderId == 0x79 && pUnit->pOrderTarget == pTarget)
                return FALSE;
            if (ai_unit_is_pursuing_order(pUnit, 0x79, pTarget, 1))
            {
                pUnit->SetOrder_xy_interrupt(6, PosS(pUnit->pSprite->wPosX), PosS(pUnit->pSprite->wPosY), 0);
                return TRUE;
            }
        }
        if (!aiscript_has_campaign(pUnit->bPlayer) && !ai_energy_is_full(pUnit))
            return FALSE;
        if (ai_can_use_tech_order(pUnit, 0x92, 0x11))
        {
            pTarget = AIFindEnsnareTarget(pUnit, nMode);
            if (pUnit->bOrderId == 0x92 && pUnit->pOrderTarget == pTarget)
                return FALSE;
            if (ai_unit_is_pursuing_order(pUnit, 0x92, pTarget, 1))
                return TRUE;
        }
        break;
    parasite:
        if (!ai_can_use_tech_order(pUnit, 0x78, 0x12))
        {
            if (!aiscript_has_campaign(pUnit->bPlayer))
                goto broodling;
            return FALSE;
        }
        pTarget = AIFindParasiteTarget(pUnit, nMode);
        if (pUnit->bOrderId == 0x78 && pUnit->pOrderTarget == pTarget)
            return FALSE;
        if (ai_unit_is_pursuing_order(pUnit, 0x78, pTarget, 4))
            return TRUE;
        break;

    case 0x47:
        if (ai_can_use_tech_order(pUnit, 0x93, 0x16))
        {
            pTarget = AIFindStasisTarget(pUnit, nMode);
            if (pUnit->bOrderId == 0x93 && pUnit->pOrderTarget == pTarget)
                return FALSE;
            if (ai_unit_is_pursuing_order(pUnit, 0x93, pTarget, 1))
                return TRUE;
        }
        if (ai_can_use_tech_order(pUnit, 0x89, 0x15))
        {
            pTarget = AIFindRecallTarget(pUnit);
            if (pUnit->bOrderId == 0x89 && pUnit->pOrderTarget == pTarget)
                return FALSE;
            if (ai_unit_is_pursuing_order(pUnit, 0x89, pTarget, 1))
                return TRUE;
        }
        break;

    case 0x43:
        if (ai_can_use_tech_order(pUnit, 0x8E, 0x13))
        {
            pTarget = AIFindPsiStormTarget(pUnit, nMode);
            if (pUnit->bOrderId == 0x8E && pUnit->pOrderTarget == pTarget)
                return FALSE;
            if (ai_unit_is_pursuing_order(pUnit, 0x8E, pTarget, 1))
                return TRUE;
        }
        if (!ai_energy_is_full(pUnit))
            return FALSE;
        if (ai_can_use_tech_order(pUnit, 0x94, 0x14))
        {
            pTarget = AIFindHallucinateTarget(pUnit, nMode);
            if (pUnit->bOrderId == 0x94 && pUnit->pOrderTarget == pTarget)
                return FALSE;
            if (ai_unit_is_pursuing_order(pUnit, 0x94, pTarget, 1))
                return TRUE;
        }
        break;

    case 0x01:
        if (ai_can_use_tech_order(pUnit, 0x73, 1))
        {
            pTarget = AIFindMagnapulseTarget(pUnit, nMode);
            if (pUnit->bOrderId == 0x73 && pUnit->pOrderTarget == pTarget)
                return FALSE;
            if (ai_unit_is_pursuing_order(pUnit, 0x73, pTarget, 1))
                return TRUE;
        }
        if (!banker_nuke_ready(pUnit->bPlayer))
            return FALSE;
        {
            CUnit *pSilo = pUnit->FindArmedSilo(1, pUnit);
            if (pSilo == 0)
                return FALSE;
            if (ai_unit_is_pursuing_order(pUnit, 0x7E, AIFindNukeTarget(pUnit), 1))
            {
                pSilo->pUnionD0->pUnion80 = pUnit;
                banker_set_last_nuke_time(pUnit->bPlayer);
                return TRUE;
            }
        }
        break;

    case 0x09:
        if (ai_can_use_tech_order(pUnit, 0x7A, 2))
        {
            pTarget = AIFindEMPTarget(pUnit, nMode);
            if (pUnit->bOrderId == 0x7A && pUnit->pOrderTarget == pTarget)
                return FALSE;
            if (ai_unit_is_pursuing_order(pUnit, 0x7A, pTarget, 1))
                return TRUE;
        }
        if (ai_can_use_tech_order(pUnit, 0x8D, 6))
        {
            pTarget = AIFindMatrixTarget(pUnit, nMode);
            if (pUnit->bOrderId == 0x8D && pUnit->pOrderTarget == pTarget)
                return FALSE;
            if (ai_unit_is_pursuing_order(pUnit, 0x8D, pTarget, 1))
                return TRUE;
        }
        if (ai_can_use_tech_order(pUnit, 0x8F, 7))
        {
            pTarget = AIFindIrradiateTarget(pUnit, nMode);
            if (pUnit->bOrderId == 0x8F && pUnit->pOrderTarget == pTarget)
                return FALSE;
            if (ai_unit_is_pursuing_order(pUnit, 0x8F, pTarget, 1))
                return TRUE;
        }
        break;

    case 0x3C:
        if (ai_can_use_tech_order(pUnit, 0xB5, 0x19))
        {
            pTarget = AIFindDisruptorTarget(pUnit, nMode);
            if (pUnit->bOrderId == 0xB5 && pUnit->pOrderTarget == pTarget)
                return FALSE;
            if (ai_unit_is_pursuing_order(pUnit, 0xB5, pTarget, 1))
                return TRUE;
        }
        break;

    case 0x22:
        if (ai_can_use_tech_order(pUnit, 0xB4, 0x18))
        {
            pTarget = AIFindCureTarget(pUnit, nMode);
            if (pUnit->bOrderId == 0xB4 && pUnit->pOrderTarget == pTarget)
                return FALSE;
            if (ai_unit_is_pursuing_order(pUnit, 0xB4, pTarget, 1))
                return TRUE;
            if (banker_is_player_medic(pUnit) && pUnit->bOrderId != 0xB4)
            {
                pTarget = AIFindParasiteCureTarget(pUnit, nMode);
                if (ai_unit_is_pursuing_order(pUnit, 0xB4, pTarget, 1))
                    return TRUE;
            }
        }
        if (!ai_energy_is_full(pUnit))
            return FALSE;
        if (!ai_can_use_tech_order(pUnit, 0xB9, 0x1E))
            return FALSE;
        pTarget = AIFindMyopiaTarget(pUnit, nMode);
        if (pUnit->bOrderId == 0xB9 && pUnit->pOrderTarget == pTarget)
            return FALSE;
        return ai_unit_is_pursuing_order(pUnit, 0xB9, pTarget, 1) != 0;

    case 0x3F:
        if (ai_can_use_tech_order(pUnit, 0xB8, 0x1D))
        {
            pTarget = AIFindPsyFeedbackTarget(pUnit, nMode);
            if (pUnit->bOrderId == 0xB8 && pUnit->pOrderTarget == pTarget)
                return FALSE;
            if (ai_unit_is_pursuing_order(pUnit, 0xB8, pTarget, 1))
                return TRUE;
        }
        if (ai_can_use_tech_order(pUnit, 0xB6, 0x1B))
        {
            pTarget = AIFindMindControlTarget(pUnit, nMode);
            if (pUnit->bOrderId == 0xB6 && pUnit->pOrderTarget == pTarget)
                return FALSE;
            if (ai_unit_is_pursuing_order(pUnit, 0xB6, pTarget, 1))
                return TRUE;
        }
        {
            int nCost = get_order_tech_energy_cost(0xBA) + get_order_tech_energy_cost(0xB6);
            if (nCost > pUnit->GetMaxEnergy())
                nCost = pUnit->GetMaxEnergy();
            if (pUnit->NeedsMoreEnergy((WORD)nCost))
                return FALSE;
        }
        if (TechUseAllowed(0x1F, pUnit->bPlayer, pUnit) != 1)
            return FALSE;
        pTarget = AIFindParalyzeTarget(pUnit, nMode);
        if (pUnit->bOrderId == 0xBA && pUnit->pOrderTarget == pTarget)
            return FALSE;
        return ai_unit_is_pursuing_order(pUnit, 0xBA, pTarget, 1) != 0;
    }
    return FALSE;
}

// sai_init -- oracle 0x48DB70 (68 B). Five sub-module inits bracketed by the
// one-shot flag's zero and the three counters' seeds. `xor ecx, ecx` in front
// of tai_captain_init is its ECX argument, i.e. a literal 0.
void sai_init()
{
  dword_69B560 = 0;
  sai_banker_init();
  tai_guardpost_init();
  tai_captain_init(0);
  sai_scripts_init();
  sai_build_init();
  dword_69B564 = 0x3C;
  dword_69B568 = 0x0F;
  dword_69B56C = 6;
}

// SAI_RemoveUnit -- oracle 0x48DBC0 (43 B).
//
// The flag is forwarded to the first callee and zeroed for the second. The
// oracle does `mov esi, ecx` and then calls TAI_Captain_RemoveUnit with no
// reload of either register -- ECX is still pUnit and EDX is still nFlag -- and
// only then does `xor edx, edx` for SAI_Build_RemoveUnit. That asymmetry is the
// whole content of the body's argument handling.
void __fastcall SAI_RemoveUnit(CUnit *pUnit, int nFlag)
{
  TAI_Captain_RemoveUnit(pUnit, nFlag);
  SAI_Build_RemoveUnit(pUnit, 0);
  TAI_GuardPost_RemoveUnit(pUnit);
  SAI_Banker_RemoveUnit(pUnit);
  pUnit->dwUnion134 = 0;
}

// read_save_ai_data -- oracle 0x48DBF0 (87 B). The six halves in order; the
// banker and the script reader are the two that also take the save version.
BOOL read_save_ai_data(intptr_t nArg0, int nArg1)
{
  return sai_read_captain_data(nArg0)
      && sai_read_guard_data(nArg0)
      && sai_read_build_data(nArg0)
      && sai_read_banker_data(reinterpret_cast<void *>(nArg0),
                              static_cast<WORD>(nArg1))
      && sai_read_script_data(reinterpret_cast<void *>(nArg0),
                              static_cast<WORD>(nArg1))
      && sai_read_town_data(nArg0);
}

// aiscript_switch_rescue -- oracle 0x48DC50 (148 B).
//
// Hands a whole nation over to the rescue-passive player type: sets
// gPlayerData[n].nType = 3, makes the nation mutually allied with all twelve
// slots, then drops every one of its live units out of the four AI subsystems
// and re-orders it.

// The alliance loop walks both directions at once and the oracle
// strength-reduces only the column. The row base is computed once
// (`lea eax, [ecx+ecx*2]` / `lea eax, [eax*4+534914h]` = &sgbAlliances_g[n][0])
// and indexed `[eax+edx*1]`; the column base (`lea esi, [ecx+534914h]` =
// &sgbAlliances_g[0][n]) is walked with `lea esi, [esi+0Ch]` and stored through
// `[esi-0Ch]`, advanced before the store. Both fall out of the plain
// subscripted pair below: the asymmetry is the addressing mode, not the source.
//
// The SetOrder call is bracketed by two identical stores
// (`mov [esi+134h], 0` on each side). The second is not dead: SetOrder is a
// cross-TU call, so dwUnion134 has to be re-zeroed after it.

// The player argument is an index and never a pointer. `lea eax, [ecx+ecx*8]`,
// `lea esi, [ecx+534914h]` and the CUnit::GetPlayerUnitList argument are its
// only three uses.
void aiscript_switch_rescue(int nPlayer)
{
  CUnit *pUnit;
  unsigned int i;

  gPlayerData[nPlayer].nType = 3;
  for (i = 0; i < 12; i++) {
    sgbAlliances_g[nPlayer][i] = 1;
    sgbAlliances_g[i][nPlayer] = 1;
  }

  for (pUnit = CUnit::GetPlayerUnitList(nPlayer); pUnit != NULL;
       pUnit = pUnit->pPlayerUnitNext) {
    if (pUnit->IsDeadOrDying())
      continue;
    TAI_Captain_RemoveUnit(pUnit, 0);
    SAI_Build_RemoveUnit(pUnit, 0);
    TAI_GuardPost_RemoveUnit(pUnit);
    SAI_Banker_RemoveUnit(pUnit);
    pUnit->dwUnion134 = 0;
    pUnit->SetOrder(0xA1);
    pUnit->dwUnion134 = 0;
  }
}

// aiscript_move_dt
//
// Walks one player's unit list and hands every Dark Templar (unit type 0x3D) or
// Dark Archon (0x4A) to tai_captain_assign_unit_to_region.
// `movzx eax, word ptr [esi+64h]` is CUnit::wType and `mov esi, [esi+6Ch]` is
// the player-list link.
//
// The two type compares share one arm, and the branch targets say so rather
// than the mnemonics: `cmp eax, 4Ah` / `jz` falls into `cmp eax, 3Dh` / `jnz`,
// so the guard is a disjunction and 0x4A is tested first. The dead check comes
// before the type check.
void aiscript_move_dt(int nArg0, int nArg1, int nArg2)
{
  CUnit *pUnit;

  for (pUnit = CUnit::GetPlayerUnitList(nArg0); pUnit != NULL;
       pUnit = pUnit->pPlayerUnitNext) {
    if (pUnit->IsDeadOrDying())
      continue;
    if (pUnit->wType == 0x4A || pUnit->wType == 0x3D)
      tai_captain_assign_unit_to_region(pUnit, nArg1, nArg2);
  }
}

// ai_mark_targets_stale -- oracle 0x48DD40 (11 B). `mov dword ptr [69B560h], 1h / ret`.
// The CUnit* in ECX is accepted and never read -- CUnit::DispatchDoorOpen
// tail-jumps here with `mov ecx, esi`, so the argument is real even though this
// body ignores it.
void __fastcall ai_mark_targets_stale(CUnit *pUnit)
{
  (void)pUnit;
  dword_69B560 = 1;
}

// ai_mark_targets_stale_on_death -- oracle 0x48DD50 (31 B). A cmov, not a
// branch:
//
//   movzx eax, word ptr [ecx+64h]        pUnit->wType
//   mov   ecx, 1h                        the 1 parked for the cmov
//   test  byte ptr [eax*4+521660h], 1h   gulUnitTypeFlags[wType] & 1
//   mov   eax, dword ptr [69B560h]
//   cmovnz eax, ecx
//   mov   dword ptr [69B560h], eax
//
// The store happens unconditionally with an if-converted value, so the guarded
// assignment is written `if (...) flag = 1;` and MSVC 2013 does the conversion.
void ai_mark_targets_stale_on_death(void *pArg0)
{
  CUnit *const pUnit = static_cast<CUnit *>(pArg0);

  if ((gulUnitTypeFlags[pUnit->wType] & 1) != 0)
    dword_69B560 = 1;
}

// init_unit_threat_scores -- oracle 0x48DD70 (5 B). A bare tail jump.
void init_unit_threat_scores()
{
  tai_compute_unit_threat_scores();
}

// sai_main_init -- oracle 0x48DD80 (15 B).
//   mov ecx, 1h / call tai_captain_init / jmp sai_townbuilder_init
void sai_main_init()
{
  tai_captain_init(1);
  sai_townbuilder_init();
}

// sai_update -- oracle 0x48DD90 (20 B). Four 5-byte slots, the last a tail jmp
// into ai_periodic_timer_tick, which lives in this TU. If this body ever grows
// by that one's whole size, /Ob2 has spliced it in and the lever is
// __declspec(noinline) on the declaration, not here.
void sai_update()
{
  sai_scripts_update();
  tai_captain_service_round_robin();
  sai_recount_town_resources();
  ai_periodic_timer_tick();
}

// sai_write_save_data -- oracle 0x48DDB0 (78 B). read_save_ai_data's mirror.
BOOL sai_write_save_data(intptr_t nArg0)
{
  return sai_write_captain_data(nArg0)
      && sai_write_guard_data(nArg0)
      && sai_write_build_data(nArg0)
      && sai_write_banker_data(reinterpret_cast<void *>(nArg0))
      && sai_write_script_data(reinterpret_cast<void *>(nArg0))
      && sai_write_town_data(nArg0);
}

// ai_set_alliance_in_area -- oracle 0x48DE00 (108 B).
//
// Stamps one alliance value onto every other nation that owns a unit inside a
// rectangle. `ret 4h`: nArg2 is the single stack word and it is the byte stored
// (`mov ebx, [ebp+8h]` once in the loop preheader, then
// `mov byte ptr [edx+ecx*4+534914h], bl`).

// The second argument is a rect32_t narrowed into a stack rect16_t, the same
// conversion as TAI_Captain.cpp's aiscript_enter_bunker family and in the same
// source order: top, bottom, left, right. The oracle's store displacements are
// -6, -2, -8, -4, which is that order against rect16_t's left/top/right/bottom
// layout; the interleaving of the movzx loads is the addressing, not a
// reordering. The declaration only says `void *`, so the cast is local.
//
// The self-skip is on the byte, widened: `movzx edx, byte ptr [ecx+4Ch]` then
// `cmp edx, esi`, bPlayer promoted to int and compared against the nation
// parameter rather than the parameter truncated.
void ai_set_alliance_in_area(int nArg0, void *pArg1, int nArg2)
{
  const rect32_t *const pArea = static_cast<const rect32_t *>(pArg1);
  rect16_t rc;
  CUnit **ppList;
  CUnit **pp;
  CUnit *pUnit;

  rc.top = static_cast<short>(pArea->top);
  rc.bottom = static_cast<short>(pArea->bottom);
  rc.left = static_cast<short>(pArea->left);
  rc.right = static_cast<short>(pArea->right);

  pp = get_units_in_rect(&rc);
  ppList = pp;
  for (pUnit = *pp; pUnit != NULL; pUnit = *++pp) {
    if (pUnit->bPlayer != nArg0)
      sgbAlliances_g[nArg0][pUnit->bPlayer] = static_cast<BYTE>(nArg2);
  }
  finish_unit_iteration(ppList);
}

// CUnit::AtMoveTarget's body, repeated here because the oracle inlines it into
// both bodies below and our copy lives in another TU, which WPO off cannot
// reach. Keep the `? 2 : 1` and the WORD cast on the Y compare; both are in
// src117/lang/CUnitBuild.cpp and both move bytes.
static __forceinline BOOL ai_at_move_target(CUnit *pUnit)
{
  CSprite *pSprite = pUnit->pSprite;

  if (pUnit->wMoveTargetX == pSprite->wPosX
      && pUnit->wMoveTargetY == PosU(pSprite->wPosY))
    return (pUnit->dwFlags & 0x80000) ? 2 : 1;

  return 0;
}

// ai_try_burrow -- oracle 0x48DE70 (108 B). An idle Terran unit that has reached
// its move target is told to lay a spider mine, if the tech is allowed.
void ai_try_burrow(intptr_t nArg0)
{
  CUnit *pUnit = reinterpret_cast<CUnit *>(nArg0);

  if ((pUnit->dwFlags & 0x10) != 0)
    return;
  if (pUnit->wType == 0x29)
    return;
  if (pUnit->bOrderId != 0x9C && pUnit->bOrderId != 2)
    return;
  if (!ai_at_move_target(pUnit))
    return;
  if (TechUseAllowed(0x0B, pUnit->bPlayer, pUnit) != 1)
    return;
  pUnit->SetOrder(0x74);
}

// ai_try_siege -- oracle 0x48DEE0 (108 B). ai_try_burrow's sibling for the other
// tech and order, guarded by the player's own AI state instead of the flag.
void ai_try_siege(intptr_t nArg0)
{
  CUnit *pUnit = reinterpret_cast<CUnit *>(nArg0);

  if (aiscript_has_campaign(pUnit->bPlayer))
    return;
  if (pUnit->bOrderId != 0x9C && pUnit->bOrderId != 2)
    return;
  if (!ai_at_move_target(pUnit))
    return;
  if (TechUseAllowed(5, pUnit->bPlayer, pUnit) != 1)
    return;
  pUnit->SetOrder(0x62);
}

// ai_periodic_timer_tick -- oracle 0x48DF50 (153 B). The AI module's timer tick:
// three independent down-counters, two of which gate work. The control flow
// below is read off the branch targets rather than the mnemonics.
//
// The decrement of dword_69B56C is a dead store on the fall-through path, where
// `mov [69B56Ch], eax` is immediately overwritten by `mov [69B56Ch], 6h`
// whenever dword_69B560 was non-zero. It survives because the source stores it
// before testing the flag, so the `if (dwB != 0)` decrement is a statement of
// its own and the reset guard re-tests dwB through the `||`.

// The 7-or-37 reload is a ternary of two constants, not a branch:
//   cmp dword ptr [5349D8h], 1194h / sbb eax, eax / and eax, -1Eh / add eax, 25h
// CF=1 (g_dwElapsedTime below 0x1194, unsigned, since the field is DWORD) gives
// eax = -1 & -0x1E + 0x25 = 7; CF=0 gives 0 + 0x25 = 37.
//
// The counter is tested on its pre-decrement value: `mov ecx, eax / dec eax /
// mov [69B564h], eax / test ecx, ecx` keeps the original in ECX, so the
// decrement is unconditional and the guard reads the value from before it.
void ai_periodic_timer_tick()
{
  if (dword_69B568 != 0) {
    dword_69B568--;
  } else {
    const DWORD dwB = dword_69B56C;

    dword_69B568 = 0x12C;
    if (dwB != 0)
      dword_69B56C = dwB - 1;
    if (dwB == 0 || dword_69B560 != 0) {
      CUnit *pUnit;

      dword_69B56C = 6;
      dword_69B560 = 0;
      for (pUnit = CUnit::GetUsedList(); pUnit != NULL; pUnit = pUnit->pNext)
        pUnit->dwFlags &= ~0x80;
    }
  }

  {
    // A post-decrement, not a read-then-subtract. `dwA = g; g = dwA - 1;` makes
    // the loaded value the live one (`mov ecx, m32`, 6 B) and the difference a
    // temporary (`lea eax, [ecx-1]`, 3 B): 9 B where the oracle spends 8, and
    // the body comes out 154 against 153. `g--` puts the subtraction in the
    // accumulator and the original in the copy, which is the oracle exactly:
    //   mov eax, [69B564h] / mov ecx, eax / dec eax / mov [69B564h], eax
    const DWORD dwA = dword_69B564--;

    if (dwA == 0) {
      dword_69B564 = (g_dwElapsedTime < 0x1194) ? 7 : 0x25;
      // Hyperion: a nation for each 8 of the game's slots, so each computer is planned
      // as often as 1.17's are (revision 17, h/playerlimits.h)
      for (int nTurn = PlayerAiTurns(); nTurn > 0; --nTurn) {
        ai_plan_next_nation();
        town_expand_tick();
      }
      tai_review_allied_strength();
    }
  }
}

// ai_nearest_enemy_of_player -- oracle 0x48DFF0 (144 B). ai_nearest_enemy_building's sibling: the nearest
// enemy of one player to a point, skipping units the region layer cannot
// reach. Same running-best shape, same two-step coordinate narrowing.
CUnit *ai_nearest_enemy_of_player(int nArg0, int nPlayer, int nX, int nY)
{
  CUnit *pBest;
  DWORD dwBest;
  CUnit *pUnit;

  pBest = NULL;
  dwBest = 0x5F5E0FF;

  for (pUnit = CUnit::GetPlayerUnitList(nPlayer); pUnit != NULL;
       pUnit = pUnit->pPlayerUnitNext) {
    CSprite *pSprite;
    upos_t wX;
    upos_t wY;
    DWORD dwDist;

    if (pUnit->IsDeadOrDying())
      continue;
    if ((gulUnitTypeFlags[pUnit->wType] & 0x10) != 0)
      continue;
    if (!IsEnemyUnit(nArg0, pUnit))
      continue;
    if (IsInaccessibleRegion(GetRegion_CUnit(pUnit)))
      continue;

    pSprite = pUnit->pSprite;
    wY = pSprite->wPosY;
    wX = pSprite->wPosX;
    dwDist = static_cast<DWORD>(
        veclen(nX, PosS(wX), nY, PosS(wY)));
    if (dwDist < dwBest) {
      dwBest = dwDist;
      pBest = pUnit;
    }
  }
  return pBest;
}

// ai_nearest_enemy_owner -- oracle 0x48E080 (58 B). The owner of the nearest enemy to a
// region's centre, or -1 when there is none.
int ai_nearest_enemy_owner(int nArg0, int nRegion)
{
  int nX;
  int nY;
  intptr_t nUnit;

  GetRegionCenter(static_cast<WORD>(nRegion), &nX, &nY);
  nUnit = ai_nearest_enemy_building(nArg0, nX, nY);
  if (nUnit != 0)
    return reinterpret_cast<CUnit *>(nUnit)->bPlayer;
  return -1;
}

// ai_nearest_enemy_building -- oracle 0x48E0C0 (145 B). Nearest live enemy unit to a region's
// centre, by veclen. `ret 4h`; returns the winner (EDI) or NULL.
//
// The third parameter's own stack slot is an out-param: `lea edx, [ebp+8h]`
// hands GetRegionCenter the address of nArg2 and it is read back as the centre
// X, while only the centre Y gets a real local. A fresh local for X grows the
// frame and cascades, so the source takes the address of the parameter.

// The two calls are one nested expression: GetRegionCenter's stack argument is
// pushed before GetRegion_xy runs, MSVC evaluating the outer call's stack
// operand first and then the inner call. Written as two statements the push
// lands after the inner call instead.
//
// The running best is unsigned -- `cmp eax, ebx / jnb` -- so the seed 0x5F5E0FF
// and the distance are DWORD; a signed local gives `jge` and one wrong byte.

// The sprite coords are loaded wide and then narrowed:
// `movzx ecx, word [eax+16h]` / `movsx eax, cx` is the two-step every veclen
// caller emits, a WORD-wide load into a local and a `(short)` cast at the call,
// not a single sign-extending load.
intptr_t ai_nearest_enemy_building(int nArg0, int nArg1, int nArg2)
{
  CUnit *pBest;
  DWORD dwBest;
  CUnit *pUnit;
  int nCenterY;

  GetRegionCenter(GetRegion_xy(nArg1, nArg2), &nArg2, &nCenterY);
  pBest = NULL;
  dwBest = 0x5F5E0FF;

  for (pUnit = CUnit::GetUsedList(); pUnit != NULL; pUnit = pUnit->pNext) {
    CSprite *pSprite;
    upos_t wX;
    upos_t wY;
    DWORD dwDist;

    if (pUnit->IsDeadOrDying())
      continue;
    if ((pUnit->dwFlags & 2) == 0)
      continue;
    if (!IsEnemyUnit(nArg0, pUnit))
      continue;

    pSprite = pUnit->pSprite;
    wY = pSprite->wPosY;
    wX = pSprite->wPosX;
    dwDist = static_cast<DWORD>(
        veclen(nArg2, PosS(wX), nCenterY, PosS(wY)));
    if (dwDist < dwBest) {
      dwBest = dwDist;
      pBest = pUnit;
    }
  }
  return reinterpret_cast<intptr_t>(pBest);
}

// aiscript_ally / aiscript_enemy -- oracle 0x48E160 / 0x48E170 (8 B each).
// `push <k> / call ai_set_alliance_in_area / ret`: both register arguments ride through
// untouched and only the stack word differs (2 for ally, 0 for enemy).
void aiscript_ally(int nArg0, intptr_t nArg1)
{
  ai_set_alliance_in_area(nArg0, (void *)nArg1, 2);
}

void aiscript_enemy(int nArg0, intptr_t nArg1)
{
  ai_set_alliance_in_area(nArg0, (void *)nArg1, 0);
}

// ai_update_unit_idle -- oracle 0x48E180 (809 B). The per-type idle behaviour of a
// computer player's unit. Case order is the oracle's block order; the
// dropship falls through into the shuttle and overlord arms.
BOOL ai_update_unit_idle(CUnit *pUnit)
{
  if (pUnit->bBackgroundOrderId == 0x6D
      && (gulUnitTypeFlags[pUnit->wType] & 0x200)
      && !pUnit->IsAttackable(NULL)
      && pUnit->dwUnion7C == 0
      && sub_432BD0(pUnit) == NULL
      && (pUnit->bOrderId < 0x7B || pUnit->bOrderId > 0x81))
    pUnit->SetBackgroundOrder(0x6E);

  switch (pUnit->wType) {
  case 0x48:
  case 0x53: {
    WORD wHangar = pUnit->GetHangarType();
    if (TTAllowed(wHangar, pUnit->bPlayer, pUnit) == 1
        && pUnit->AddBuildQueue(wHangar)
        && !pUnit->sub_41C260())
      pUnit->ReplaceBackgroundOrder(0x3F);
    return 0;
  }
  case 0x0B:
    if ((pUnit->lHitPoints + 0xFF) >> 8 != pUnit->GetLife()) {
      CUnit *pTarget = pUnit->sub_432BB0();
      if (pTarget != NULL) {
        if (pUnit->IsUnitInside())
          pUnit->SetOrder(0x6F);
        else
          pUnit->SetOrder_CUnit(0x31, pTarget);
        return 1;
      }
    }
  case 0x45:
    if (AIMoveWhereNeeded(pUnit))
      return 1;
  case 0x2A:
    if (pUnit->IsUnitInside()) {
      pUnit->SetOrder(0x6F);
      return 1;
    }
    break;
  case 0x7C:
  case 0x90:
  case 0x92:
  case 0xA2:
    pUnit->SetOrder(0x12);
    return 1;
  case 0x02: {
    int nX;
    int nY;
    if (TechUseAllowed(3, pUnit->bPlayer, pUnit) == 1
        && tai_pick_spider_mine_spot(pUnit, &nX, &nY)) {
      pUnit->SetOrder_xy(0x84, nX, nY);
      return 1;
    }
    break;
  }
  case 0x05: {
    TGuard *pGuard = (TGuard *)pUnit->dwUnion134;
    if (pGuard->bType == 1 && !guard_return_to_post(pUnit, 0)
        && TechUseAllowed(5, pUnit->bPlayer, pUnit) == 1
        && pUnit->pOrderQueueHead == NULL) {
      pUnit->SetOrder(0x62);
      return 1;
    }
    break;
  }
  case 0xAC:
    if (static_cast<BYTE>(pUnit->wEnergy >> 8) >= 0x32) {
      for (CUnit *pOther = CUnit::GetPlayerUnitList(pUnit->bPlayer); pOther != NULL;
           pOther = pOther->pPlayerUnitNext) {
        if (pOther->CanRechargeShieldsAt(pUnit) && pOther->dwUnion7C == 0
            && !tai_soldier_captain_is_attacking(pOther, 0)
            && ((int)pOther->lShields >> 8) <= 0x28) {
          pUnit->SetOrder_xy(0x43, PosS(pOther->pSprite->wPosX),
                             PosS(pOther->pSprite->wPosY));
          return 0;
        }
      }
    }
    break;
  case 0x47: {
    CSprite *pSprite = pUnit->pSprite;
    int nPlayer = pUnit->bPlayer;
    mrect_t  rc;
    rc.left = PosS(pSprite->wPosX - 0x280);
    rc.right = PosS(pSprite->wPosX + 0x280);
    rc.top = PosS(pSprite->wPosY - 0x280);
    rc.bottom = PosS(pSprite->wPosY + 0x280);
    CUnit **pp = get_units_in_rect(&rc);
    CUnit **const ppList = pp;
    CUnit *pOther;
    for (; (pOther = *pp) != NULL; pp++) {
      if (pOther->bPlayer == nPlayer && pOther->dwUnion7C != 0
          && (gulUnitTypeFlags[pOther->wType] & 1) == 0) {
        pUnit->SetOrder_xy(0x0E, PosS(pOther->pSprite->wPosX),
                           PosS(pOther->pSprite->wPosY));
        finish_unit_iteration(ppList);
        return 1;
      }
    }
    finish_unit_iteration(ppList);
    break;
  }
  }
  return 0;
}

// ai_can_use_tech_order -- oracle 0x48E580 (59 B). The trailing
// `dec / neg / sbb / inc` is the oracle's own `== 1`, not a truth test.
int ai_can_use_tech_order(CUnit *pUnit, int nOrder, int nTech)
{
  if (pUnit->NeedsMoreEnergy(get_order_tech_energy_cost(static_cast<order_id_t>(nOrder))))  // Hyperion: a mod's order
    return 0;
  return TechUseAllowed(nTech, pUnit->bPlayer, pUnit) == 1;
}

// ai_unit_is_pursuing_order -- oracle 0x48E5C0 (63 B). The byte at CUnit+0x92
// is reached by offset because src117/h/CUnit.h still carries it inside
// _unmapped_90; it is a bitmask of orders already asked for. A bit already set
// returns FALSE, so both rejections share the one `xor eax, eax` tail.
BOOL ai_unit_is_pursuing_order(CUnit *pUnit, int nOrder, CUnit *pTarget, BYTE bAsked)
{
  BYTE *pbAsked;

  pbAsked = reinterpret_cast<BYTE *>(pTarget) + offsetof(CUnit, bUnion92);
  if (pTarget == NULL || (*pbAsked & bAsked) != 0)
    return FALSE;
  if (pUnit->bOrderId != nOrder) {
    pUnit->SetOrder_CUnit(nOrder, pTarget);
    *pbAsked |= bAsked;
  }
  return TRUE;
}
