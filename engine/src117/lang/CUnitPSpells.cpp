// CUnitPSpells.cpp -- protoss unit spells: stasis, paralyze, hallucination, psi feedback. Bodies are in oracle VA order.
//
// A callee defined in this file can be spliced into a caller here by /Ob2, while the
// oracle, built with WPO off, cannot splice across a TU boundary: a callee left in another
// TU reproduces that for free, and __declspec(noinline) is the lever when one must live
// here.

#include "../h/CUnitPSpells.h"
#include "../h/playervision.h"  // Hyperion: the slots past 7
#include "../h/CUnit.h"
#include "../h/CSprite.h"
#include "../h/CThingy.h"
#include "../h/CFlingy.h"
#include "../h/CUnitBuild.h"     // ours: ClipToMap + point16_t
#include "../h/TAI_AreaStrength.h"  // ours: tai_assign_unit_strength
#include "../h/TrigAct.h"        // ours: CreateUnitAt, sub_4B6A60
#include "../h/count.h"          // ours: the three unit tallies
#include "../h/CUnitTSpells.h"   // ours: gamesnd_play
#include "../h/CUnitZSpells.h"   // ours: register_unit_for_ai
#include "../gen/globals117.h"
#include "../h/handicap.h"  // Hyperion: a player's handicap

// The area search behind the two location dispatchers. CBullet.h declares the
// same C symbol with a one-argument filter; both views decorate to
// @find_first_unit_near@20, and the filter here needs the context register.
typedef BOOL(__fastcall *pfn_pspell_filter_t)(void *pCandidate, intptr_t nContext);

extern "C" CUnit *__fastcall find_first_unit_near(int x, int y, int nRadius,
                                        pfn_pspell_filter_t pfnFilter,
                                        intptr_t nContext);

// Hyperion: Hallucinate and HallucinateHangar take `this` in ECX with their remaining argument
// on the stack, and 1.17's calls went through __thiscall pointers that left EDX out. On x64
// there is one convention, the arguments in order, so a call through such a pointer put the
// player in the unused second parameter and left the third to chance: the first Hallucination
// cast crashed. The calls are direct, with all three arguments.

// The bodies, in oracle VA order.

// Spawns a disruption web at a clipped location and caps its lifetime.
void CUnit::ApplyDisruptorLocation(int wX, int wY, BYTE bPlayer)
{
  point16_t pos = {PosS(wX), PosS(wY)};
  ClipToMap(&pos, 0x69);
  CUnit *pUnit = CreateUnitAt(0x69, pos.x, pos.y, 0x0B);
  if (pUnit == NULL) {
    ShowCreateError(bPlayer);
    return;
  }
  pUnit->dwFlags |= 0x00200000;
  pUnit->pSprite->bElevationLevel = 0x0B;
  pUnit->FinishedMaking();
  WORD wTime = pUnit->wTimeToLive;
  if (wTime == 0 || 0x168 < wTime)
    pUnit->wTimeToLive = 0x168;
}

// Plays the maelstrom overlay and sound, then paralyzes everything in range.
void CUnit::ApplyParalyzeLocation(int x, int y, intptr_t nSource)
{
  CThingy *pThingy = CThingy::AllocThingy(0x202, x, y, 0);
  if (pThingy != NULL) {
    pThingy->pSprite->bElevationLevel = 0x11;
    pThingy->UpdateVisibility();
  }
  gamesnd_play(0x428, reinterpret_cast<CUnit *>(nSource), 1, 0);
  find_first_unit_near(x, y, 0x30, ApplyUnitParalyze, nSource);
}

// The stasis-field counterpart, without the sound.
void CUnit::ApplyStasisLocation(int x, int y, intptr_t nSource)
{
  CThingy *pThingy = CThingy::AllocThingy(0x169, x, y, 0);
  if (pThingy != NULL) {
    pThingy->pSprite->bElevationLevel = 0x11;
    pThingy->UpdateVisibility();
  }
  find_first_unit_near(x, y, 0x30, ApplyUnitStasis, nSource);
}

// find_first_unit_near's maelstrom filter. Hallucinations die instead of being paralyzed.
BOOL CUnit::ApplyUnitParalyze(void *pCandidate, intptr_t nSource)
{
  CUnit *pUnit = static_cast<CUnit *>(pCandidate);
  DWORD dwUnitFlags = pUnit->dwFlags;

  if (dwUnitFlags & 0x40000000) {
    pUnit->Suicide();
    return 0;
  }
  if (pUnit != reinterpret_cast<CUnit *>(nSource) &&
      (dwUnitFlags & 0x04000000) == 0 &&
      (gulUnitTypeFlags[pUnit->wType] & 1) == 0 &&
      (dwUnitFlags & 0x10) == 0 &&
      (gulUnitTypeFlags[pUnit->wType] & 0x10000) != 0)
    pUnit->UnitApplyParalyze(0x16);
  return 0;
}

// find_first_unit_near's stasis filter.
BOOL CUnit::ApplyUnitStasis(void *pCandidate, intptr_t nSource)
{
  CUnit *pUnit = static_cast<CUnit *>(pCandidate);

  if (pUnit != reinterpret_cast<CUnit *>(nSource) &&
      (pUnit->dwFlags & 0x04000000) == 0 &&
      (gulUnitTypeFlags[pUnit->wType] & 1) == 0 &&
      (pUnit->dwFlags & 0x10) == 0)
    pUnit->UnitApplyStasis(0x83);
  return 0;
}

// Completes a hallucinated unit and repeats down its subunit chain.
void CUnit::sub_430960()
{
  count_remove_existence(this);
  SetButtonset(0xF4);
  lHitPoints = HandicapLife(gxUnitLife[wType], bPlayer);  // Hyperion: at the owner's handicap
  dwFlags |= 0x40000001;
  count_add_existence(this);
  count_add_completion(this, 0);
  SetOrder(gubUnitOrderInitialize[wType]);
  if (gPlayback != 0 || bPlayer == g_LocalNationID)
    pSprite->SetShift(0);
  if ((gulUnitTypeFlags[wType] & 0x10) == 0) {
    tai_assign_unit_strength(this);
    SetBackgroundOrder(0x95);
    if (subunit != NULL)
      subunit->sub_430960();
  }
}

// The hallucination counterpart, extended down the hangar list.
void CUnit::sub_430A00()
{
  count_remove_existence(this);
  count_remove_completion(this, 1);
  SetButtonset(0xF4);
  dwFlags |= 0x40000000;
  count_add_existence(this);
  count_add_completion(this, 0);
  if (gPlayback != 0 || bPlayer == g_LocalNationID)
    pSprite->SetShift(0);
  if ((gulUnitTypeFlags[wType] & 0x10) == 0) {
    WORD wMax = static_cast<WORD>(GetMaxTimeToLive());
    if (wTimeToLive == 0 || wMax < wTimeToLive)
      wTimeToLive = wMax;
    tai_assign_unit_strength(this);
    SetBackgroundOrder(0x95);
    if (subunit != NULL)
      subunit->sub_430A00();
    if (wType == 0x48 || wType == 0x52 || wType == 0x53 || wType == 0x51) {
      if (wType == 0x53 || wType == 0x51) {
        CUnit *pFighter = pHangarDockedHead;
        for (int nRemaining = bHangarCount; nRemaining != 0; --nRemaining) {
          pFighter->sub_430A00();
          pFighter = pFighter->pHangarNext;
        }
      } else {
        HangarSelfDestruct();
      }
    }
  }
}

// Clones a carrier's docked and building fighters onto the hallucinated copy.
void HallucinateHangar(intptr_t nCarrier, int nUnused, void *pProto)
{
  CUnit *pCarrier = reinterpret_cast<CUnit *>(nCarrier);
  CUnit *pSource = static_cast<CUnit *>(pProto);
  (void)nUnused;

  int nRemaining = pSource->bHangarCount;
  CUnit *pFighter = pSource->pHangarDockedHead;
  while (nRemaining != 0) {
    --nRemaining;
    CUnit *pClone =
        reinterpret_cast<CUnit *>(Hallucinate(reinterpret_cast<intptr_t>(pFighter), 0, pCarrier->bPlayer));
    if (pClone != NULL) {
      pClone->pHangarParent = pCarrier;
      pCarrier->HangarConstructionFinished(pClone);
    }
    pFighter = pFighter->pHangarNext;
  }

  nRemaining = pSource->bHangarCountBuilding;
  pFighter = pSource->pHangarDeployedHead;
  while (nRemaining != 0) {
    --nRemaining;
    CUnit *pClone =
        reinterpret_cast<CUnit *>(Hallucinate(reinterpret_cast<intptr_t>(pFighter), 0, pCarrier->bPlayer));
    if (pClone != NULL) {
      pClone->pHangarParent = pCarrier;
      pCarrier->HangarConstructionFinished(pClone);
    }
    pFighter = pFighter->pHangarNext;
  }
}

// Drops a thingy at a unit's position, one elevation level above it.
CThingy *sub_430B90(int nThingyType, intptr_t nUnit)
{
  CUnit *pUnit = reinterpret_cast<CUnit *>(nUnit);
  upos_t wY = pUnit->pSprite->wPosY;
  upos_t wX = pUnit->pSprite->wPosX;

  CThingy *pThingy = CThingy::AllocThingy(nThingyType, PosS(wX),
                                          PosS(wY), 0);
  if (pThingy != NULL) {
    pThingy->pSprite->bElevationLevel =
        static_cast<BYTE>(pUnit->pSprite->bElevationLevel + 1);
    pThingy->UpdateVisibility();
  }
  return pThingy;
}

// Hallucination cast: makes up to two copies of the order target and pays for it.
void CUnit::DispatchHallucinate()
{
  WORD wCost;

  if (sub_434B80(0x14, sub_423150(1) << 5, 0x383, &wCost)) {
    int nTries = 2;
    while (nTries != 0) {
      --nTries;
      CUnit *pClone =
          reinterpret_cast<CUnit *>(Hallucinate(reinterpret_cast<intptr_t>(pOrderTarget), 0, bPlayer));
      if (pClone == NULL)
        break;
      if (!pClone->sub_430FA0()) {
        pClone->Destroy();
        break;
      }
    }
    SubtractEnergy(wCost);
    gamesnd_play(0x26A, pOrderTarget, 1, 0);
    pOrderTarget->CreateOverlay(0x221, 0, 0, 0);
    EndDispatchOrder();
  }
}

// A hallucination dies the moment any status effect lands on it.
void CUnit::DispatchHallucination()
{
  if (wDefMatrixHitPoints != 0 || bStimTimer != 0 || bEnsnareTimer != 0 ||
      bLockdownTimer != 0 || bIrradiateTimer != 0 || bStasisTimer != 0 ||
      UnitParasite(this).Any() || _unmapped_11B != 0 || bBloodBoilTimer != 0 ||  // Hyperion
      UnitMyopia(this).Any() || bParalyzeTimer != 0)  // Hyperion: bUnion123 (stage D)
    Suicide();
}

// Mind control: hallucinations die, everything else changes hands. The two
// early returns spell their own EndDispatchOrder, which MSVC tail-merges with
// the last one; the single-guard form puts `this` in the other register.
void CUnit::DispatchMindControl()
{
  WORD wCost;
  CUnit *pTarget = pOrderTarget;

  if (pTarget == NULL) {
    EndDispatchOrder();
    return;
  }
  if (bPlayer == pTarget->bPlayer) {
    EndDispatchOrder();
    return;
  }
  if (!sub_434B80(0x1B, sub_423150(1) << 5, 0x52E, &wCost))
    return;

  if (pTarget->dwFlags & 0x40000000) {
    pTarget->Suicide();
  } else {
    pTarget->CreateOverlay(pTarget->GetSizeType() + 0x3CD, 0, 0, 0);
    sub_430F60(pTarget);
    pTarget->EndDispatchOrder();
  }
  lShields = 0;
  SubtractEnergy(wCost);
  gamesnd_play(0x426, pTarget, 1, 0);
  EndDispatchOrder();
}

// Psionic storm feedback: burns the target's energy for equal damage.
void CUnit::DispatchPsyFeedback()
{
  WORD wCost;
  CUnit *pTarget = pOrderTarget;

  if (pTarget == NULL) {
    EndDispatchOrder();
    return;
  }
  if ((gulUnitTypeFlags[pTarget->wType] & 0x200000) == 0) {
    EndDispatchOrder();
    return;
  }
  if (pTarget->dwFlags & 0x40000000) {
    EndDispatchOrder();
    return;
  }
  if (!sub_434B80(0x1D, sub_423150(1) << 5, 0x532, &wCost))
    return;

  if (pTarget->dwFlags & 0x40000000) {
    sub_430B90(pTarget->GetSizeType() + 0x1F4, reinterpret_cast<intptr_t>(pTarget));
    SubtractEnergy(wCost);
    pTarget->Suicide();
    EndDispatchOrder();
    return;
  }

  WORD wEnergy = pTarget->wEnergy;
  if (wEnergy != 0) {
    pTarget->Damage(wEnergy, 0x6A, 1, 1, this, bPlayer);
    pTarget->wEnergy = 0;
    SubtractEnergy(wCost);
    gamesnd_play(0x425, pTarget, 1, 0);
    if (!pTarget->IsDeadOrDying()) {
      pTarget->CreateOverlay(pTarget->GetSizeType() + 0x3D3, 0, 0, 0);
      EndDispatchOrder();
      return;
    }
    sub_430B90(pTarget->GetSizeType() + 0x1F4, reinterpret_cast<intptr_t>(pTarget));
  }
  EndDispatchOrder();
}

// Creates one hallucinated copy of a unit and hands it to the AI.
intptr_t Hallucinate(intptr_t nProto, int nUnused, int nPlayer)
{
  CUnit *pProto = reinterpret_cast<CUnit *>(nProto);
  (void)nUnused;

  WORD wUnitType = pProto->wType;
  upos_t wY = pProto->pSprite->wPosY;
  upos_t wX = pProto->pSprite->wPosX;
  CUnit *pClone = CreateUnitAt(wUnitType, PosS(wX),
                               PosS(wY), nPlayer);
  if (pClone == NULL) {
    CUnit::ShowCreateError(static_cast<BYTE>(nPlayer));
    return 0;
  }

  pClone->sub_430960();
  WORD wMax = static_cast<WORD>(pClone->GetMaxTimeToLive());
  if (pClone->wTimeToLive == 0 || wMax < pClone->wTimeToLive)
    pClone->wTimeToLive = wMax;
  if (pClone->wType == 0x53 || pClone->wType == 0x51)
    HallucinateHangar(reinterpret_cast<intptr_t>(pClone), 0, pProto);
  register_unit_for_ai(pClone, pClone);
  return reinterpret_cast<intptr_t>(pClone);
}

// Hands a mind-controlled unit to the caster's player; hallucinations die.
void CUnit::sub_430F60(CUnit *pTarget)
{
  if (pTarget->dwFlags & 0x40000000) {
    pTarget->Suicide();
    return;
  }
  sub_4B6A60(pTarget, bPlayer);
  if (pTarget->wType == 0x3F)
    pTarget->wEnergy = 0;
}

// Places a fresh hallucination next to its original and faces it at random.
int CUnit::sub_430FA0()
{
  mpoint_t  in;
  mpoint_t  out;  // Hyperion: a map point

  in.x = pSprite->wPosX;
  in.y = pSprite->wPosY;
  if (!::FindPlaceForUnit(this, &in, &out, NULL, 0, 0))
    return 0;

  SetUnitPosition(out.x, out.y);
  if ((dwFlags >> 16) & 1) {
    int nDirection = net_randbetween(0, 0x20, 0x1E);
    pSprite->FaceDirection(nDirection);
    reinterpret_cast<CFlingy *>(this)->FaceDirection(nDirection * 8);
  }
  Show();
  return 1;
}

// Redraws the stasis overlay after a load, keeping the longer of the two
// timers. Same shape as CUnitTSpells.cpp's SyncParalyze: the widening of the
// saved timer is its own statement, which is what parks it zero-extended in a
// callee-saved register across the overlay call.
void CUnit::SyncStasis()
{
  BYTE bTime = bStasisTimer;
  bStasisTimer = 0;
  unsigned int nTime = bTime;
  CreateOverlay(GetSizeType() + 0x16D, 0, 0, 0);
  dwFlags |= 0x04000000;
  BYTE bCurrent = bStasisTimer;
  bStasisTimer = static_cast<BYTE>(nTime) > bCurrent
                     ? static_cast<BYTE>(nTime) : bCurrent;
  sub_434C60();
}

// Puts a unit in stasis, extending an existing field rather than shortening it.
void CUnit::UnitApplyStasis(int nTime)
{
  if (bStasisTimer == 0) {
    CreateOverlay(GetSizeType() + 0x16D, 0, 0, 0);
    dwFlags |= 0x04000000;
  }
  BYTE bNow = bStasisTimer;
  bStasisTimer = static_cast<BYTE>(nTime) > bNow ? static_cast<BYTE>(nTime)
                                                 : bNow;
  sub_434C60();
}

// Ends a stasis field and restores the unit's buttons.
void CUnit::RemoveStasis()
{
  bStasisTimer = 0;
  if ((dwFlags & 0x400) == 0 && bLockdownTimer == 0 && bParalyzeTimer == 0) {
    SetButtonset(wType);
    status_set_update();
  }
  if ((gulUnitTypeFlags[wType] & 0x20000000) == 0)
    dwFlags &= ~0x04000000;
  DestroyOverlays(0x16D, 0x16F);
}
