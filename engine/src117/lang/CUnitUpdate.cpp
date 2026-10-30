// CUnitUpdate.cpp -- part of the per-frame order dispatch: the medic and support orders,
// the zerg lifecycle, and the transport load slice. Bodies are in oracle VA order.
//
// Two units.dat columns are dual-spelled TU-locally below, both named off the sheet and
// indexed by the unbiased unit type: guwUnitTimeToBuild at 0x5238B0, WORD[228], the build
// time per type, and gubUnitCargoHold at 0x523FE0, BYTE[228], the cargo-hold capacity.

#include "oc_data.h"  // Hyperion: OC_VA
#include <string.h>

#include "../h/CUnitUpdate.h"
#include "../h/CUnit.h"
#include "../h/CUnitCombat.h"
#include "../h/SAI_Main.h"        // ai_update_unit_spells
#include "../h/CUnitTSpells.h"
#include "../h/CUnitZSpells.h"
#include "../h/CTarget.h"
#include "../h/CSprite.h"
#include "../h/CFlingy.h"
#include "../h/SAI_Banker.h"
#include "../h/saveload.h"
#include "../h/order.h"
#include "../h/TAI_GuardPost.h"    // TGuard, which W55OrderQ134 views
#include "../gen/globals117.h"

#ifdef OC_DLL_MODE
  #ifndef guwUnitTimeToBuild
  #define guwUnitTimeToBuild (reinterpret_cast<WORD *>(OC_VA(0x5238B0u)))
  #endif
  #ifndef gubUnitCargoHold
  #define gubUnitCargoHold (reinterpret_cast<BYTE *>(OC_VA(0x523FE0u)))
  #endif
#else
extern WORD guwUnitTimeToBuild[228] OC_UNALIGNED;
extern BYTE gubUnitCargoHold[] OC_UNALIGNED;
#endif

// The queued-order node CUnit::dwUnion134 points at. It is bigger than COrder's proven
// 0x14 stride, so it is not spelled through COrder, and it stays TU-local.
// Hyperion: the record is a TGuard (TAI_GuardPost.h): bNodeId is its bType
// and wNodeX and wNodeY its wPosX and wPosY. The view holds TGuard's two links and
// its unit pointer so that the fields stay on TGuard's on x64 too, as asserted
// below.
struct W55OrderQ134 {
  /*0x00*/ void *_links[2];
  /*0x08*/ BYTE bNodeId;
  /*0x09*/ BYTE _pad09[3];
  /*0x0C*/ void *_p0C;
  /*0x10*/ WORD _w10;
  /*0x12*/ upos_t wNodeX;  // Hyperion: TGuard's positions, 4 bytes on x64 (stage E)
  /*0x14*/ upos_t wNodeY;
};
OC_X86_LAYOUT(offsetof(W55OrderQ134, bNodeId) == 0x08, "W55OrderQ134::bNodeId [ecx+8h] byte");
OC_X86_LAYOUT(offsetof(W55OrderQ134, wNodeX)  == 0x12, "W55OrderQ134::wNodeX  [ecx+12h] word");
OC_X86_LAYOUT(offsetof(W55OrderQ134, wNodeY)  == 0x14, "W55OrderQ134::wNodeY  [ecx+14h] word");
static_assert(offsetof(W55OrderQ134, bNodeId) == offsetof(TGuard, bType), "W55OrderQ134 is TGuard");
static_assert(offsetof(W55OrderQ134, wNodeX) == offsetof(TGuard, wPosX), "W55OrderQ134 is TGuard");
static_assert(offsetof(W55OrderQ134, wNodeY) == offsetof(TGuard, wPosY), "W55OrderQ134 is TGuard");

// CUnit::DispatchMindControl (0x430CE0) is not here: its oracle TU is CUnitPSpells.cpp,
// and two definitions of one symbol are a link error.

// CUnit::DispatchMedic, oracle 0x4361B0 (146 B)
void CUnit::DispatchMedic()
{
  if (dwUnion134 != 0 && ai_update_unit_spells(this, 0) != 0)
    return;

  CUnit *pHeal = FindHealTarget();
  if (pHeal != NULL) {
    SetOrder(0xB0);
    reinterpret_cast<CTarget *>(this)->SetFocusTarget(pHeal);
    SetMoveTarget_CUnit(pHeal);
    return;
  }

  W55OrderQ134 *po = reinterpret_cast<W55OrderQ134 *>(dwUnion134);
  if (po == NULL)
    return;
  if (po->bNodeId != 1) {
    SetOrder(0x9C);
    return;
  }
  upos_t nx = po->wNodeX;
  if (pSprite->wPosX != nx ||
      PosU(pSprite->wPosY) != po->wNodeY)
    SetOrder_xy(0xA3, PosS(nx), PosS(po->wNodeY));
}

// CUnit::DispatchMedicHeal, oracle 0x436250 (177 B)
void CUnit::DispatchMedicHeal()
{
  int nClass = sub_4371A0(pOrderTarget);
  if (nClass == 0) {
    Stop();
    EndDispatchOrder();
    return;
  }

  if (bOrderState == 0) {
    if (pOrderTarget == NULL ||
        !ApproachMoveTarget_CUnit(pOrderTarget) ||
        AtMoveTarget() == 2) {
      this->EndDispatchOrder();
      return;
    }
    if (!IsWithinTargetRange(0x1E, pOrderTarget))
      return;
    Stop();
    bOrderState = 1;
    return;
  }

  if (nClass == 1) {
    if (!(bOrderFlags & nClass)) {
      pSprite->SetAnimation(0xD, nClass);
      if (pOrderQueueHead == NULL || GetFirstQueuedOrder() != 0xB3)
        SetNextOrder(0xB3);
    }
    CSprite *pSpr = pOrderTarget->pSprite;
    reinterpret_cast<CFlingy *>(this)->SetDestination(PosS(pSpr->wPosX), pSpr->wPosY);
    return;
  }
  bOrderState = 0;
}

// A medic holding position: settle once, then either idle, walk to the heal
// target, or look for a new one. Case 2 shares case 0's arm (the oracle's
// table routes slots 0 and 2 to the same block).
void CUnit::DispatchMedicHoldPos()
{
  if (bOrderState == 0) {
    StopUnitMovement();
    dwFlags |= 0x8000000;
    if (subunit != NULL)
      subunit->dwFlags |= 0x8000000;
    bOrderState = 1;
    pSprite->SetAnimation(0xC, 1);
  }

  switch (sub_4371A0(pOrderTarget)) {
  case 3:
    pOrderTarget = NULL;
    // fall through
  case 0:
  case 2:
    pSprite->SetAnimation(0xC, 1);
    break;
  case 1:
    if (!(bOrderFlags & 1)) {
      pSprite->SetAnimation(0xD, 1);
      if (pOrderQueueHead == NULL || GetFirstQueuedOrder() != 0xB3)
        SetNextOrder(0xB3);
    }
    {
      CSprite *pSpr = pOrderTarget->pSprite;
      reinterpret_cast<CFlingy *>(this)->SetDestination(PosS(pSpr->wPosX), pSpr->wPosY);
    }
    return;
  default:
    break;
  }

  if (bUnion54 == 0) {
    bUnion54 = 0xF;
    pOrderTarget = FindHealTarget();
    if (pOrderTarget != NULL) {
      wOrderTargetX = pOrderTarget->pSprite->wPosX;
      wOrderTargetY = pOrderTarget->pSprite->wPosY;
    }
  }
}

// CUnit::DispatchEnterTransport (oracle 0x4376D0, 198 B) is in
// src117/lang/CUnitTransport.cpp, its oracle TU.

// CUnit::DispatchLoadUnitTransport, oracle 0x4377A0 (163 B)
// dwUnion134 is truth-tested only; bUnion54 is set to 7 early and re-tested; the tail jmp
// through SetNextOrder_CUnit(0x5D) into ManageQueuedOrders pins the void return.
void CUnit::DispatchLoadUnitTransport()
{
  CUnit *pTarget = this->pOrderTarget;

  if (pTarget == NULL)
    return EndDispatchOrder();
  if (pTarget->dwUnion134 != 0 && pTarget->bOrderId != 0x60)
    return EndDispatchOrder();
  if (this->bOrderState != 0)
    return;
  if (!pTarget->CanEnterTransport(this))
    return EndDispatchOrder();
  if (this->bUnion54 != 0)
    return;

  this->bUnion54 = 7;
  if (!this->IsWithinTargetRange(1, pTarget)) {
    this->SetDestination_CUnit(pTarget);
    return;
  }
  this->StopUnitMovement();

  if (pTarget->bOrderId != 0x5C && pTarget->bOrderId != 0x60)
    pTarget->SetOrder_CUnit(0x5C, this);

  if (this->dwUnion134 != 0) {
    this->bOrderState = 1;
    return;
  }
  this->SetNextOrder_CUnit(0x5D, pTarget);
  this->ManageQueuedOrders();
}

// CUnit::DispatchUnload (oracle 0x437BA0, 198 B) is in src117/lang/CUnitTransport.cpp, its
// oracle TU.

// CUnit::DispatchZergBldgMorph
void CUnit::DispatchZergBldgMorph()
{
  int nMorph = wBuildQueue[bBuildQueueSlot];
  if (bldg_get_unit_cost(bPlayer, static_cast<WORD>(nMorph), TRUE, TRUE) == 0) {
    SetOrder_interrupt(gubUnitOrderGuard[wType], 0);
    return;
  }
  bldg_deduct_resources(bPlayer);
  dwFlags &= ~1u;
  SetButtonset(0xEC);
  SyncConstructionGraphic(TRUE);
  // Statement order is the oracle's own: the table load precedes the `push 2Dh` and the
  // store sits between the push and the call.
  wRemainingBuildTime = guwUnitTimeToBuild[nMorph];
  SetOrder(0x2D);
  pSprite->SetAnimation(0xD, 1);
  status_set_update();
}

// w55b iteration -- CUnit::DispatchInfest, oracle 0x43CF10 (198 B)
// Two shapes decide this body. EndDispatchOrder is spelled as one statement per copy of
// the oracle's epilogue rather than a shared tail, and the dead consumer after the
// neg/sbb/neg/inc chain is written out (`if (!nAir) goto camp;`), which gives both the
// chain and the dead je in one block.
void CUnit::DispatchInfest()
{
  CUnit *pTarget = pOrderTarget;
  switch (bOrderState) {
  case 0:
    if (pTarget == NULL) {
      EndDispatchOrder();
    } else if (!pTarget->IsInfestable()) {
      gamesnd_error(this, status_get_str(0x366),
                     static_cast<DWORD>(bPlayer));
      EndDispatchOrder();
    } else if (!SetDestination_CUnit(pTarget)) {
      // dead-end: plain return, order not ended
    } else {
      bOrderState = 1;
    }
    return;
  case 1: {
    if (!(wMoveTargetX == pSprite->wPosX &&
          wMoveTargetY == PosU(pSprite->wPosY))) {
camp:
      if (pTarget != NULL)
        ApproachMoveTarget_CUnit(pTarget);
      return;
    }
    int nAir = (dwFlags & 0x80000) ? 2 : 1;
    if (!nAir)
      goto camp;
    if (pTarget != NULL && pTarget->IsInfestable() && nAir != 2) {
      SetOrder_CUnit(0x1D, pTarget);
    } else {
      EndDispatchOrder();
    }
    return;
  }
  default:
    return;
  }
}

// CUnit::DispatchInfestEnd
void CUnit::DispatchInfestEnd()
{
  CUnit *pTarget = pOrderTarget;

  switch (bOrderState) {
  case 0:
    // Burrowed attacker making contact with a victim.
    if (pTarget != NULL && pTarget->IsInfestable() &&
        pTarget->bOrderId != 0xF) {
      Hide();
      pTarget->SetOrder_CUnit(0xF, this);
      bOrderState = 3;
      wRemainingBuildTime = 0x3C;
      return;
    }
    // Failed all three victim checks: stand up and join the common tail.
    StopUnitMovement();
    break;
  case 3: {
    // Countdown finished: emerge.
    WORD wTime = wRemainingBuildTime;
    if (wTime != 0) {
      --wTime;
      wRemainingBuildTime = wTime;
      return;
    }
    mpoint_t  ptIn;
    mpoint_t  ptOut;  // Hyperion: a map point
    ptIn.x = pSprite->wPosX;
    ptIn.y = PosU(pSprite->wPosY);
    if (!FindPlaceForUnit(this, &ptIn, &ptOut, NULL, 0, 0))
      return;
    // Whole-dword passes (oracle pushes the raw dwords, no widening): the
    // callee fills only the low word of each slot.
    SetUnitPosition(*reinterpret_cast<int *>(&ptOut.x),
                    *reinterpret_cast<int *>(&ptOut.y));
    StopUnitMovement();
    Show();
    break;
  }
  default:
    return;
  }

Retreat:
  if (pUnion74 == NULL)
    SetOrder_interrupt(gubUnitOrderGuard[wType], 0);
  EndUninterruptableDispatchOrder();
}

