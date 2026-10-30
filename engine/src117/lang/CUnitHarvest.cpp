// CUnitHarvest.cpp -- the worker and resource harvest block: the order dispatchers a
// mining worker runs (harvest, gas-mine precondition, minerals-wait, interrupt), the
// intrusive doubly-linked list of workers a resource keeps and the four bodies that splice
// and unsplice it, the resource-amount bookkeeping and patch animation, and the eight
// predicates the rest of the engine asks about resources and refineries. Bodies are in
// ascending oracle VA order, which under /Gy and /Ox is 1.17's own declaration order.
//
// TU membership matters in three directions at once, all for CUnit::RemoveHarvestUnit
// (0x424750): it is called for real by CUnit::DispatchHarvest4 and CUnit::sub_4244C0,
// whose oracle bodies emit a call, and not inlined into CUnit::sub_423ED0 or
// CUnit::sub_424170, whose oracle bodies contain its instructions verbatim -- those two
// written as a call come out 77 bytes short each. So the unlink is written out literally
// at those two sites and called at the other two; do not tidy the duplication into a call,
// and do not reach for __forceinline. The same holds for CUnit::IsTargetRefinery, written
// out inside CUnit::DispatchGasMinePrecondition with the roles swapped.

#include "oc_data.h"  // Hyperion: OC_VA
#include "../port/gameplay/port_ai.h"  // Hyperion: a computer's level
#include "../h/mutators.h"  // Hyperion: resources that never run dry
#include "platform.h"   // ours: BOOL/BYTE/WORD/DWORD
#include "CUnit.h"      // ours: CUnit + every field this TU touches, all asserted
#include "CSprite.h"    // ours: CSprite::SetAnimation, for UpdateResourceAnim
#include "CUnitHarvest.h"  // ours: the one free function + the gamesnd thunk
#include "SAI_Banker.h"    // ours: banker_worker_bonus_allowed
#include "CFlingy.h"        // ours: CFlingy::FaceDirection, AngleBetween
#include "CUnitZSpells.h"   // ours: FindPlaceForUnit
#include "sai_influence.h"  // ours: LineClip
#include "CUnitCombat.h"    // ours: GetVectorComponents

#include "../gen/globals117.h"

// gulUnitTypeFlags -- the units.dat per-type flags table, DWORD[228] at 0x521660, indexed
// unbiased by CUnit::wType. The dual spelling is mandatory and not a style: the shipped
// form only switches spellings inside src117/gen/globals117.h, so a plain in-TU extern
// stays a symbol in both compiles and leaves the baked slot unchecked. One #ifndef per
// name, never one over a block, since the generated set grows over time and a single guard
// would go false and undeclare every name under it.
#ifdef OC_DLL_MODE
  #ifndef gulUnitTypeFlags
  #define gulUnitTypeFlags (reinterpret_cast<DWORD *>(OC_VA(0x521660u)))
  #endif
#else
extern DWORD gulUnitTypeFlags[228] OC_UNALIGNED;
#endif


// Empties a resource's harvester list, clearing each worker's links and slot.
void CUnit::sub_423400() {
  CUnit *pCur = this->pUnionD4_HarvestPrev;
  this->bUnionD3 = 0;
  while (pCur) {
    CUnit *pNext = pCur->pUnionD8_HarvestNext;
    if (pCur->pUnionD4_HarvestPrev)
      pCur->pUnionD4_HarvestPrev->pUnionD8_HarvestNext = pNext;
    else
      this->pUnionD4_HarvestPrev = pNext;
    if (pCur->pUnionD8_HarvestNext)
      pCur->pUnionD8_HarvestNext->pUnionD4_HarvestPrev =
          pCur->pUnionD4_HarvestPrev;
    pCur->pUnionD4_HarvestPrev = 0;
    pCur->pUnionD8_HarvestNext = 0;
    pCur->pUnionD0 = 0;
    pCur = pNext;
  }
}


// CUnit::DispatchHarvest -- 0x423480, 311 B, no frame, four tail jumps. At 311 B with some
// fifteen branches, four things in the source shape are fixed by the branch targets.
//
// With no frame and no stack local, bCarryResource cannot be a named local: it is read
// into AL at entry and read again from memory once two calls have clobbered AL.
// pOrderTarget is a local in EDI, but two later sites re-read the field, which is the
// reload a call in between needs.
//
// The state dispatch is `if (bState != 0) { if (bState != 3) return; ... }` with the
// state-0 block last: only the nested form puts the zero test first and the zero body
// last. Block placement follows statement position in both polarities -- the null-target
// test jumps forward over an inline Stop and End, an early return, while the pair above it
// jump past the whole success body, making the same two calls an else arm. The range test
// reads `pTarget->wType` on both sides, or the compares fold into a biased subtract; and
// SetDestination_CUnit's failure stays a plain return on the shared epilogue, a rel32 jz.
void CUnit::DispatchHarvest() {
  CUnit *pTarget = this->pOrderTarget;
  if (this->bCarryResource != 0 && (this->bCarryResource & 3) == 0) {
    this->StopUnitMovement();
    this->EndDispatchOrder();
    return;
  }
  if (!(this->dwFlags & 0x800000)) {
    this->dwFlags |= 0x800000;
    COrder *pQueued = this->pOrderQueueHead;
    if (pQueued)
      this->PrependOrder(0x97, pQueued);
    else
      this->AppendOrder(0x97, 0);
  }
  if (this->bOrderState != 0) {
    if (this->bOrderState != 3)
      return;
    BOOL nAtTarget = this->AtMoveTarget();
    if (nAtTarget) {
      if (pTarget && pTarget->IsTargetRefinery(this)) {
        if (nAtTarget == 2) {
          this->bOrderState = 0;
        } else if (this->bCarryResource & 1) {
          this->SetOrder(0x54);
        } else {
          this->SetNextOrder_CUnit(0x52, pTarget);
          this->EndDispatchOrder();
        }
      } else {
        this->StopUnitMovement();
        this->EndDispatchOrder();
      }
    }
    return;
  }
  if (!pTarget) {
    this->StopUnitMovement();
    this->EndDispatchOrder();
    return;
  }
  if (0xB0 <= pTarget->wType && pTarget->wType <= 0xB2) {
    this->SetNextOrder_Target(
        0x55, this->OrderTarget());
    this->EndDispatchOrder();
    return;
  }
  if (!this->SetDestination_CUnit(pTarget))
    return;
  this->dwUnionC4_SavedOrderTargetPos = PosPack(this->wOrderTargetX, this->wOrderTargetY);
  this->pUnionC8_SavedOrderTarget = this->pOrderTarget;
  this->bOrderState = 3;
}


// Runs a worker's visit inside a refinery: enter, wait, then come out
// carrying gas and head back to a depot.
void CUnit::DispatchInteractWithGasMine() {
  CUnit *pTarget = this->pOrderTarget;
  if (!(this->pSprite->flags.b & 0x20)) {
    if (!(pTarget && (pTarget->dwFlags & 1) &&
          (pTarget->wType == 0x6E || pTarget->wType == 0x9D ||
           pTarget->wType == 0x95) &&
          pTarget->bPlayer == this->bPlayer &&
          (this->bCarryResource == 0 || (this->bCarryResource & 3)))) {
      this->StopUnitMovement();
      this->EndUninterruptableDispatchOrder();
      if (this->bUnionCE) {
        CUnit *pResource = this->pUnionD0;
        this->bUnionCE = 0;
        this->pUnionD0 = 0;
        if (pResource) {
          --pResource->bUnionD3;
          CUnit *pOther = pResource->sub_423ED0();
          if (pOther)
            pOther->sub_4240D0(pResource);
        }
      }
      return;
    }
  }
  if (!(this->dwFlags & 0x800000)) {
    this->dwFlags |= 0x800000;
    COrder *pQueued = this->pOrderQueueHead;
    if (pQueued)
      this->PrependOrder(0x97, pQueued);
    else
      this->AppendOrder(0x97, 0);
  }
  BYTE bState = this->bOrderState;
  if (bState != 0) {
    if (bState != 5 || this->bUnion54 != 0)
      return;
    BYTE bDir = this->bUnion21;
    mpoint_t pos;  // Hyperion: a map point
    if (!sub_424590(this, pTarget, &pos, &bDir)) {
      this->bUnion54 = 0x12;
      return;
    }
    reinterpret_cast<CFlingy *>(this)->FaceDirection(bDir);
    this->SetUnitPosition(pos.x, pos.y);
    this->Show();
    if (pTarget)
      pTarget->SetHarvestInventory(this);
    if (this->bUnionCE)
      this->sub_424130(this->pUnionD0);
    this->RemoveOrder(0x97);
    if (!this->pOrderQueueHead) {
      if (this->bCarryResource & 1)
        this->SetOrder_interrupt(0x54, 0);
      else
        this->SetOrder_interrupt(gubUnitOrderGuard[this->wType], 0);
    }
    this->PrependOrder(0x97, this->pOrderQueueHead);
    this->EndUninterruptableDispatchOrder();
    return;
  }
  this->sub_430390(1);
  this->bOrderState = 5;
  this->bUnion54 = 0x25;
}


// 0x4237A0 -- CUnit::DispatchGasMinePrecondition, 101 B. IsTargetRefinery's exact
// predicate inline over pOrderTarget, plus a carry test.
//
// The carry test is a disjunction inside a conjunction, and the two jz targets are the
// proof: a zero carry jumps forward to the continue point, skipping the mask test, while a
// nonzero carry with no low bits set jumps to the failure tail. So it reads
// `(carry == 0 || (carry & 3))`, not `(carry & 3) == 0` and not a second guard.
//
// The bOrderState test is a nested guard and not a conjunct: its jnz lands on the then
// arm's own `pop esi / ret` while all six earlier failures run StopUnitMovement and
// EndDispatchOrder, the same structure as DispatchHarvestMineralsWait above.
//
// The predicate is written inline rather than as `pTarget->IsTargetRefinery(this)` because
// the expression must not depend on an inlining decision. The roles are swapped against
// that function, and `cmp al, [esi+4Ch]` keeps the refinery's byte on the left.
void CUnit::DispatchGasMinePrecondition() {
  CUnit *pTarget = this->pOrderTarget;
  if (pTarget && (pTarget->dwFlags & 1) &&
      (pTarget->wType == 0x6E || pTarget->wType == 0x9D ||
       pTarget->wType == 0x95) &&
      pTarget->bPlayer == this->bPlayer &&
      (this->bCarryResource == 0 || (this->bCarryResource & 3))) {
    if (this->bOrderState == 0)
      this->sub_424230(pTarget);
  } else {
    this->StopUnitMovement();
    this->EndDispatchOrder();
  }
}


// Mines one load from the targeted mineral field: face it, wait out the
// mining animation, then take the minerals and head back.
void CUnit::DispatchHarvestingMinerals() {
  CUnit *pTarget = this->pOrderTarget;
  if (pTarget && 0xB0 <= pTarget->wType && pTarget->wType <= 0xB2) {
    if (!(this->dwFlags & 0x800000)) {
      this->dwFlags |= 0x800000;
      COrder *pQueued = this->pOrderQueueHead;
      if (pQueued)
        this->PrependOrder(0x97, pQueued);
      else
        this->AppendOrder(0x97, 0);
    }
    switch (this->bOrderState) {
    case 5:
      if (this->bUnion54 == 0) {
        this->pSprite->SetAnimation(8, 1);
        pTarget->SetHarvestInventory(this);
        this->sub_424130(pTarget);
        this->RemoveOrder(0x58);
        if (this->bCarryResource & 2)
          this->SetOrder(0x5A);
        else
          this->SetOrder(0x55);
      }
      return;
    case 0: {
      if (!this->IsFacingMoveTarget())
        return;
      int dx;
      int dy;
      reinterpret_cast<CFlingy *>(this)->GetVectorComponents(this->bUnion21, 0x14, &dx, &dy);
      this->wOrderTargetX = this->pSprite->wPosX + dx;
      this->wOrderTargetY = this->pSprite->wPosY + dy;
      this->bOrderState = 4;
    }
    case 4:
      this->pSprite->SetAnimation(0x0F, 1);
      this->bUnion54 = 0x4B;
      this->bOrderState = 5;
      return;
    }
    return;
  }
  if (this->bUnionCE) {
    CUnit *pResource = this->pUnionD0;
    this->bUnionCE = 0;
    this->pUnionD0 = 0;
    if (pResource) {
      --pResource->bUnionD3;
      CUnit *pOther = pResource->sub_423ED0();
      if (pOther)
        pOther->sub_4240D0(pResource);
    }
  }
  this->RemoveOrder(0x58);
  this->SetOrder(0x55);
}


// AtMoveTarget's test over an x the caller already loaded; the oracle inlines
// it here while calling AtMoveTarget itself a few lines earlier. y is read
// from the sprite only after x matches.
static int HarvestAtMoveTarget(CUnit *pUnit, upos_t x, CSprite *pSpr) {
  if (pUnit->wMoveTargetX == x && pUnit->wMoveTargetY == PosU(pSpr->wPosY))
    return (pUnit->dwFlags & 0x80000) ? 2 : 1;
  return 0;
}

// Walks a worker to a mineral field and queues the mining order, switching
// to a free field when the chosen one is taken.
// A taken field with no free neighbour falls into state 3; that fallthrough
// is what saves EBX at the switch and lets the two retarget tails merge.
void CUnit::DispatchHarvestMinerals() {
  CUnit *pTarget = this->pOrderTarget;
  if (this->bCarryResource == 0 || (this->bCarryResource & 3)) {
    if (!(this->dwFlags & 0x800000)) {
      this->dwFlags |= 0x800000;
      COrder *pQueued = this->pOrderQueueHead;
      if (pQueued)
        this->PrependOrder(0x97, pQueued);
      else
        this->AppendOrder(0x97, 0);
    }
    point16_t &saved = *reinterpret_cast<point16_t *>(&this->dwUnionC4_SavedOrderTargetPos);
    if (!pTarget || !(0xB0 <= pTarget->wType && pTarget->wType <= 0xB2)) {
      mpoint_t  pt;
      if (saved.x == 0 && saved.y == 0) {
        pt.x = this->pSprite->wPosX;
        pt.y = this->pSprite->wPosY;
      } else {
        pt = saved;
        saved.x = 0;
        saved.y = 0;
      }
      pTarget = this->sub_431F70(pt);
      if (pTarget) {
        this->pOrderTarget = pTarget;
        this->wOrderTargetX = pTarget->pSprite->wPosX;
        this->wOrderTargetY = pTarget->pSprite->wPosY;
        this->bOrderState = 0;
      } else {
        this->StopUnitMovement();
        this->EndDispatchOrder();
        return;
      }
    }
    switch (this->bOrderState) {
    case 1:
      if (!this->IsWithinTargetRange(10, pTarget) && !this->AtMoveTarget())
        return;
      if (pTarget->bUnionD3) {
        mpoint_t  pos;
        pos.x = this->pSprite->wPosX;
        pos.y = this->pSprite->wPosY;
        CUnit *pOther = this->FindFreeMineral(pos, 0);
        if (pOther && pOther != pTarget) {
          if (this->SetDestination_CUnit(pOther)) {
            this->pUnionC8_SavedOrderTarget = pOther;
            this->pOrderTarget = pOther;
            int x = PosS(pOther->pSprite->wPosX);
            saved.x = x;
            this->wOrderTargetX = x;
            int y = pOther->pSprite->wPosY;
            saved.y = y;
            this->wOrderTargetY = y;
          }
          return;
        }
      }
      this->bOrderState = 3;
    case 3: {
      CSprite *pSpr = this->pSprite;
      upos_t x = pSpr->wPosX;
      int nAt = HarvestAtMoveTarget(this, x, pSpr);
      if (!nAt)
        return;
      if (nAt == 2) {
        mpoint_t  pos;
        pos.x = x;
        pos.y = pSpr->wPosY;
        pTarget = this->sub_431F70(pos);
        if (!pTarget) {
          this->EndDispatchOrder();
          return;
        }
        if (this->SetDestination_CUnit(pTarget)) {
          this->pUnionC8_SavedOrderTarget = pTarget;
          this->pOrderTarget = pTarget;
          int x = PosS(pTarget->pSprite->wPosX);
          saved.x = x;
          this->wOrderTargetX = x;
          int y = pTarget->pSprite->wPosY;
          saved.y = y;
          this->wOrderTargetY = y;
        }
        return;
      }
      if (this->bCarryResource & 2) {
        this->SetOrder(0x5A);
        return;
      }
      reinterpret_cast<CFlingy *>(this)->SetDestination(this->wOrderTargetX, this->wOrderTargetY);
      this->SetNextOrder_CUnit(0x56, pTarget);
      this->EndDispatchOrder();
      return;
    }
    case 0:
      if (!this->SetDestination_CUnit(this->pOrderTarget))
        return;
      this->dwUnionC4_SavedOrderTargetPos = PosPack(this->wOrderTargetX, this->wOrderTargetY);
      this->pUnionC8_SavedOrderTarget = this->pOrderTarget;
      this->bOrderState = 1;
      return;
    }
    return;
  }
  this->StopUnitMovement();
  this->EndDispatchOrder();
}


// 0x423C30 -- CUnit::DispatchHarvestMineralsWait, 55 B.
//
// The bOrderState test is not part of the same condition as the other three, and the
// displacements are the only thing that says so: its jnz lands on the `pop esi` that tails
// the else block, skipping the sub_424230 call, whereas all three earlier failures land on
// the block that runs SetOrder(0x55). So bOrderState is a nested guard inside the then arm
// and not a fourth conjunct; folded into the && chain it would have branched with its
// neighbours.
//
// The range test reads the WORD field twice in source so MSVC CSEs only the load and
// leaves two compares standing; a named local folds it into a biased subtract instead.
// Operand order is read straight off the oracle: `cmp si, dx` (constant first) is
// `0xB0 <= wType`, `cmp dx, si` is `wType <= 0xB2`. ESI rather than ECX because ECX must
// stay live as `this` for the +0x4E read and both calls. Void: both exits are a bare
// `pop esi / ret` with nothing defining EAX.
void CUnit::DispatchHarvestMineralsWait() {
  CUnit *pTarget = this->pOrderTarget;
  if (pTarget && 0xB0 <= pTarget->wType && pTarget->wType <= 0xB2) {
    if (this->bOrderState == 0)
      this->sub_424230(pTarget);
  } else {
    this->SetOrder(0x55);
  }
}


// 0x423C70 -- CUnit::DispatchHarvestInterrupt, 74 B.
//
// The outer jz skips the `pop edi` and the inner one does not, and that asymmetry is what
// proves the guard is a plain if around the whole EDI-using block rather than an early
// return: EDI is only pushed inside the taken arm, so the not-taken path must rejoin below
// the pop, where an early return would have had to duplicate the tail jump.
//
// pUnionD0 is read into a local before both slots are cleared, which is the only order
// that makes the body do anything -- the oracle's load sits above its own stores, so this
// is source order and not scheduling. sub_423ED0 is called on the resource
// (`mov ecx, edi`) and its CUnit * result becomes the `this` of sub_4240D0 with the
// resource as the argument; the BOOL result is discarded. The tail jmp makes it void.
void CUnit::DispatchHarvestInterrupt() {
  if (this->bUnionCE) {
    CUnit *pResource = this->pUnionD0;
    this->bUnionCE = 0;
    this->pUnionD0 = 0;
    if (pResource) {
      --pResource->bUnionD3;
      CUnit *pOther = pResource->sub_423ED0();
      if (pOther)
        pOther->sub_4240D0(pResource);
    }
  }
  this->EndUninterruptableDispatchOrder();
}


// CUnit::DispatchHarvest4 -- 0x423CC0, 27 B. The order-dispatcher shape: a guarded call,
// then a tail jump. The trailing jmp is only available to a void function and is what pins
// the return type here, not the arity. The guarded call is a member call on the resource
// with `this` as the argument, so ECX is loaded from +0xD0 and ESI pushed.
void CUnit::DispatchHarvest4() {
  if (this->pUnionD0)
    this->pUnionD0->RemoveHarvestUnit(this);
  this->EndUninterruptableDispatchOrder();
}


// Carries a worker back to its resource depot and deposits what it holds.
void CUnit::DispatchReturnResource() {
  if (this->bCarryResource == 0 || (this->bCarryResource & 3) == 0) {
    this->StopUnitMovement();
    this->EndDispatchOrder();
    return;
  }
  if (!(this->dwFlags & 0x800000)) {
    this->dwFlags |= 0x800000;
    COrder *pQueued = this->pOrderQueueHead;
    if (pQueued)
      this->PrependOrder(0x97, pQueued);
    else
      this->AppendOrder(0x97, 0);
  }
  CUnit *pDest = this->pOrderTarget;
  if (!pDest || !pDest->IsResourceDest()) {
    pDest = 0;
    this->bOrderState = 0;
  }
  switch (this->bOrderState) {
  case 0:
    if (!pDest) {
      pDest = this->sub_431F00();
      if (!pDest) {
        if (this->dwUnion134 != 0) {
          this->SetOrder(0x9C);
          return;
        }
        this->bUnion85 = 0x4B;
        return;
      }
    }
    this->pOrderTarget = pDest;
    if (!this->SetDestination_CUnit(pDest))
      return;
    this->bOrderState = 1;
    return;
  case 1: {
    BOOL nAtTarget = this->AtMoveTarget();
    if (!nAtTarget)
      return;
    if (nAtTarget == 2) {
      this->pOrderTarget = 0;
      this->bOrderState = 0;
      return;
    }
    int nOrder = this->dwUnion134 != 0 ? 0x9C : (((~this->bCarryResource & 1) << 2) | 0x51);
    if (this->pUnionC8_SavedOrderTarget)
      this->SetNextOrder_CUnit(nOrder, this->pUnionC8_SavedOrderTarget);
    else
      this->SetNextOrder(nOrder);
    this->EndDispatchOrder();
    this->DepositInventory((intptr_t)pDest);
    if (pDest->IsZergTownhall())
      pDest->StabilizeHatcheryBounds(this);
    return;
  }
  }
}


// CUnit::GetHarvest -- 0x423E40, 133 B, `ret 4h`, one WORD local in a register. The
// parameter is BYTE-width and so is the return: two `cmp byte ptr [ebp+8h], 0h` reads (an
// int would be a dword compare) and three exits that define only AL.
//
// There is no local at all -- the field is read four times and MSVC forwards the store --
// and three source shapes establish it. `WORD w = (WORD)(this->wResourceAmount - 8);`
// gives the oracle's `add eax, -8h` but adds a movzx for the truncating initialisation
// and then narrows the later compare to 32 bits: 135 B. The same local with a 16-bit `-=`
// keeps both compares 16-bit but materialises the addend with wrap semantics,
// `add eax, 0FFF8h` against the oracle's sign-extended form: 135 B again. Assigning
// `this->wResourceAmount - 8` back to the field is 133 B exactly -- int arithmetic buys
// the imm8, the store truncates on its own, and the later reads forward the stored AX.
//
// The suicide arm is the out-of-line one, which pins the guard's polarity: the oracle
// falls through to UpdateResourceAnim and jumps forward to Suicide, so Suicide is last.
BYTE CUnit::GetHarvest(BYTE bSuicideWhenEmpty) {
  // Hyperion: a trip is 1.17's 8, or a rich resource's 11 or 16 (rich bases)
  const int nTrip = MutatorTrip(static_cast<int>(this - CUnit::Head()), !this->IsMineralSrc());
  // Hyperion: a resource a mutator makes endless gives its trip and keeps what it holds
  if (MutatorEndless(!this->IsMineralSrc()))
    return static_cast<BYTE>(nTrip);
  if (this->wResourceAmount >= nTrip) {
    this->wResourceAmount = this->wResourceAmount - nTrip;
    if (bSuicideWhenEmpty) {
      if (this->wResourceAmount != 0) {
        this->UpdateResourceAnim();
        return static_cast<BYTE>(nTrip);
      }
      this->Suicide();
      return static_cast<BYTE>(nTrip);
    }
    if (this->wResourceAmount < nTrip)
      gamesnd_show_player_error(this->bPlayer, 0x14, 0x36B);
    return static_cast<BYTE>(nTrip);
  }
  if (bSuicideWhenEmpty) {
    this->Suicide();
    return (BYTE)this->wResourceAmount;
  }
  this->wResourceAmount = 0;
  return 2;
}


// CUnit::sub_423ED0 -- 0x423ED0, 182 B, bare ret, no stack parameter. Two branch targets
// are the whole structure.
//
// There is no break: the keep-this-node store falls straight into the advance and the loop
// keeps running, so the walk keeps the last acceptable node and not the first. A break
// would have put a jump out of the loop there and changed the whole block order.
//
// `mov edx, ecx` at entry is why `this` survives: ECX is clobbered inside the loop by
// `mov cl, byte ptr [eax+4Dh]`, so the else arm of the unlink writes through EDX -- the
// evidence that the head lives on `this` and not on the node, the same asymmetry
// RemoveHarvestUnit encodes.
//
// The unlink is RemoveHarvestUnit's body written out, not called and not inlined: the call
// spelling is 77 bytes short and no definition ordering recovers it.
CUnit *CUnit::sub_423ED0() {
  CUnit *pFound = 0;
  CUnit *pCur = this->pUnionD4_HarvestPrev;
  while (pCur) {
    if (!(pCur->dwFlags & 0x400) && pCur->bLockdownTimer == 0 &&
        pCur->bStasisTimer == 0 && pCur->bParalyzeTimer == 0 &&
        (pCur->bOrderId == 0x52 || pCur->bOrderId == 0x56))
      pFound = pCur;
    pCur = pCur->pUnionD8_HarvestNext;
  }
  if (pFound) {
    if (pFound->pUnionD4_HarvestPrev)
      pFound->pUnionD4_HarvestPrev->pUnionD8_HarvestNext =
          pFound->pUnionD8_HarvestNext;
    else
      this->pUnionD4_HarvestPrev = pFound->pUnionD8_HarvestNext;
    if (pFound->pUnionD8_HarvestNext)
      pFound->pUnionD8_HarvestNext->pUnionD4_HarvestPrev =
          pFound->pUnionD4_HarvestPrev;
    pFound->pUnionD4_HarvestPrev = 0;
    pFound->pUnionD8_HarvestNext = 0;
    pFound->pUnionD0 = 0;
    pFound->RemoveOrder(0x59);
  }
  return pFound;
}


// CUnit::sub_423F90 -- 0x423F90, 61 B, bare ret, a rotated list walk.
//
// All three rejections land on the same `mov eax, [eax+6Ch]` and only the final equality
// exits, so the four tests are one && conjunction guarding a return and not a chain of
// continues -- and the last one carries the value out, jumping straight to the shared
// `pop esi / ret` with EAX already holding the node. The trailing `xor eax, eax` is
// emitted even though EAX is provably zero on both paths that reach it; it is the
// `return 0` statement, not dead code.
//
// The rotated shape -- entry test, body, then advance, test and jump back to the body head
// -- is MSVC's canonical `for (p = head; p; p = p->next)`. `movzx ecx, byte ptr [esi+4Ch]`
// is a full-width widen into the argument register, consistent with GetPlayerUnitList's
// int parameter. `test byte ptr [ecx*4+521660h], 8h` is gulUnitTypeFlags indexed unbiased
// by wType, with MSVC narrowing the low-byte mask to a byte test on the DWORD table.
CUnit *CUnit::sub_423F90() {
  for (CUnit *p = CUnit::GetPlayerUnitList(this->bPlayer); p;
       p = p->pPlayerUnitNext) {
    if (p->bOrderId == 0x53 && (gulUnitTypeFlags[p->wType] & 8) &&
        (p->pSprite->flags.b & 0x20) && p->pOrderTarget == this)
      return p;
  }
  return 0;
}


// Takes one load from this resource and puts it in the harvester's hands.
void CUnit::SetHarvestInventory(CUnit *pHarvester) {
  BYTE bMinerals = 0;
  int nImage;
  switch (this->wType) {
  case 0x6E: nImage = 0x193; break;
  case 0x9D: nImage = 0x18F; break;
  case 0x95: nImage = 0x191; break;
  case 0xB0: case 0xB1: case 0xB2: nImage = 0x18D; bMinerals = 1; break;
  default: return;
  }
  BYTE bAmount = this->GetHarvest(bMinerals);
  if (bAmount < 8)
    nImage++;
  if (bAmount) {
    if (pHarvester->bCarryResource & 3) {
      pHarvester->RemoveInventoryOverlay();
      pHarvester->bCarryResource = 0;
    }
    // Hyperion: 1.17's bonus, or a Cheater level's (port_ai.h)
    bAmount += AiHarvestExtra(pHarvester, bAmount, banker_worker_bonus_allowed(pHarvester) ? 1 : 0);
    pHarvester->AddInventory(nImage, bAmount, bMinerals);
    // Hyperion: whether the load is a rich resource's, for its look (rich bases)
    MutatorCarry(static_cast<int>(pHarvester - CUnit::Head()), static_cast<int>(this - CUnit::Head()));
  }
}


// CUnit::sub_4240D0 -- 0x4240D0, 93 B. Two calls to one callee that MSVC tail-merges down
// to the call alone, and the template for sub_424230 and one arm of DispatchHarvest.
//
// Both arms push their own arguments and only the call is shared, which rules out the
// obvious spelling: hoisting the order id into a local and making one call emits one push
// pair. So the source has two textual calls and MSVC merged their tails. The taken arm
// also skips the `mov ecx, esi`, nothing having clobbered ECX on that path while the
// fall-through had SetNextOrder eat it; that is MSVC's ECX tracking and falls out of the
// same source.
//
// The +0xCE and +0xD0 stores are written before the order-id test in source and MSVC
// hoists the cmp above them -- scheduling, not statement order. The guard is written
// positively and wraps the body, not as an early `if (busy) return 0;`: both spellings are
// one test and one zero return, but the early-return form lays the epilogue down inline at
// the test where the oracle jumps forward past the whole body to a tail.
BOOL CUnit::sub_4240D0(CUnit *pResource) {
  if (pResource->bUnionD3 == 0) {
    pResource->bUnionD3 = 1;
    this->bUnionCE = 1;
    this->pUnionD0 = pResource;
    if (this->bOrderId == 0x52) {
      this->SetNextOrder_CUnit(0x53, pResource);
    } else {
      this->SetNextOrder(0x58);
      this->SetNextOrder_CUnit(0x57, pResource);
    }
    this->EndDispatchOrder();
    return 1;
  }
  return 0;
}


// CUnit::sub_424130 -- 0x424130, 58 B, `ret 4h`. `this` is the worker and the argument the
// resource, and the first two instructions after the prologue prove it: the +0xCE and
// +0xD0 pair is written through ECX, so those are `this`'s fields, where its mirror
// sub_424170 below writes the same pair through the argument.
//
// The two clears happen unconditionally, before the null test on the argument; only the
// release of the resource is guarded. The `mov ecx, esi` before sub_423ED0 is what says
// that call is on the resource and not on `this` -- compare sub_424170, where the same
// call needs no reload because there the resource already is `this`.
void CUnit::sub_424130(CUnit *pResource) {
  this->bUnionCE = 0;
  this->pUnionD0 = 0;
  if (pResource) {
    pResource->bUnionD3--;
    CUnit *pNext = pResource->sub_423ED0();
    if (pNext)
      pNext->sub_4240D0(pResource);
  }
}


// CUnit::sub_424170 -- 0x424170, 187 B, `ret 4h`. The mirror: `this` is the resource and
// the argument the worker, with `mov eax, dword ptr [ebp+8h] / cmp byte ptr [eax+0CEh], 0h`
// putting the worker's flag on the argument. That is the whole difference from sub_424130,
// and it costs both bodies' register assignment to get backwards.
//
// The flag-set arm is the inline one, with its own `pop/pop/ret 4h`, and the list search is
// laid out after it: an early-return spelling, where the `if (flag) { ...; return; }` body
// sits where it is written and the search follows.
//
// The tail is RemoveHarvestUnit's body written out against the node the walk found -- ECX,
// not the argument in EAX. The two are provably equal at that point and MSVC did not
// substitute one for the other, so the source spells the cursor; writing pHarvester there
// moves the register.
void CUnit::sub_424170(CUnit *pHarvester) {
  if (pHarvester->bUnionCE) {
    pHarvester->bUnionCE = 0;
    pHarvester->pUnionD0 = 0;
    if (this) {
      this->bUnionD3--;
      CUnit *pNext = this->sub_423ED0();
      if (pNext)
        pNext->sub_4240D0(this);
    }
    return;
  }
  CUnit *pCur = this->pUnionD4_HarvestPrev;
  while (pCur) {
    if (pCur == pHarvester) {
      if (pCur->pUnionD4_HarvestPrev)
        pCur->pUnionD4_HarvestPrev->pUnionD8_HarvestNext =
            pCur->pUnionD8_HarvestNext;
      else
        this->pUnionD4_HarvestPrev = pCur->pUnionD8_HarvestNext;
      if (pCur->pUnionD8_HarvestNext)
        pCur->pUnionD8_HarvestNext->pUnionD4_HarvestPrev =
            pCur->pUnionD4_HarvestPrev;
      pCur->pUnionD4_HarvestPrev = 0;
      pCur->pUnionD8_HarvestNext = 0;
      pCur->pUnionD0 = 0;
      return;
    }
    pCur = pCur->pUnionD8_HarvestNext;
  }
}


// CUnit::sub_424230 -- 0x424230, 179 B, `ret 4h`. `this` is the worker, the argument the
// resource.
//
// The first arm is sub_4240D0's body repeated in source, and the difference in the bytes
// is instructive: there the two SetNextOrder_CUnit calls tail-merged down to one call,
// here they do not, each arm carrying its own call, its own EndDispatchOrder and its own
// epilogue. Arm 1 reaches SetNextOrder_CUnit with ECX still holding `this` while arm 2
// has had SetNextOrder eat it and needs a reload, and two tails that are not textually
// identical cannot be merged. So both arms must stay two textual calls here too.
//
// The order-id compare sits between the +0xCE and +0xD0 stores -- MSVC hoisting a test
// above a store it does not alias, so write the stores first. +0xD4 is read twice, once
// for the store into `this`->+0xD8 and again for the null test; hoisting it into a local
// emits one load and loses two instructions.
void CUnit::sub_424230(CUnit *pResource) {
  this->bUnion85 = 0;
  this->bOrderState = 2;
  if (pResource->bUnionD3 == 0) {
    pResource->bUnionD3 = 1;
    this->bUnionCE = 1;
    this->pUnionD0 = pResource;
    if (this->bOrderId == 0x52) {
      this->SetNextOrder_CUnit(0x53, pResource);
    } else {
      this->SetNextOrder(0x58);
      this->SetNextOrder_CUnit(0x57, pResource);
    }
    this->EndDispatchOrder();
  } else {
    if (this->pUnionD0 == 0) {
      this->pUnionD0 = pResource;
      this->pUnionD8_HarvestNext = pResource->pUnionD4_HarvestPrev;
      this->pUnionD4_HarvestPrev = 0;
      if (pResource->pUnionD4_HarvestPrev)
        pResource->pUnionD4_HarvestPrev->pUnionD4_HarvestPrev = this;
      pResource->pUnionD4_HarvestPrev = this;
    }
    this->SetNextOrder(0x59);
  }
}


// 0x4242F0 -- CUnit::sub_4242F0, 8 B. The whole body is one widening read: a BYTE field
// returned through an int-width return is exactly `movzx`, and the declared BOOL is what
// makes it a widening integral conversion rather than a byte move. The value is not
// normalised to 0 or 1 -- there is no neg/sbb pair -- so the oracle hands the caller the
// raw count and callers that treat it as a boolean rely on nonzero being true.
BOOL CUnit::sub_4242F0() {
  return this->bUnionD3;
}


// CUnit::IsRefinery -- 0x424300, 47 B. A flag gate with an early `return 0`, then a type
// set test.
//
// The block placement is the tell, and it is what distinguishes this from a single
// `return (f & 1) && (...);` expression: an explicit early `return 0;` statement makes MSVC
// lay the zero return down right where it is written, and the last type test then jumps
// backward into it instead of forward to a tail. Written as one && expression the whole
// thing collapses to a forward-branching chain with the zero at the end -- same semantics,
// different bytes. Compare CUnit::IsResourceDest, whose flag test jumps forward to a tail
// zero: there the source really is one expression.
//
// dwFlags is tested as a BYTE by MSVC narrowing a low-byte mask: the field is a full DWORD,
// and `& 1` on a DWORD is what emits `test byte ptr`.
BOOL CUnit::IsRefinery() {
  if (!(this->dwFlags & 1))
    return 0;
  return this->wType == 0x6E || this->wType == 0x9D || this->wType == 0x95;
}


// 0x424330 -- CUnit::IsTargetRefinery, 67 B. CUnit::IsRefinery plus an owner test, and the
// difference from IsRefinery is the block placement.
//
// Every failure branches forward to one shared zero at the tail, so this is a single &&
// expression. Its sibling IsRefinery has the identical flag test but lays the zero return
// down inline at that test and reaches it backward from the last type compare -- there the
// source really does open with an explicit `if (!(f & 1)) return 0;`. Same predicate, same
// operand order, and the two spellings are distinguishable only by where the zero sits.
//
// The type-set encoding is not source-steerable: 0x6E fits a sign-extended imm8 so it
// compares against the zero-extended EAX directly, while 0x9D and 0x95 exceed it and must
// go through a register. EDX rather than IsRefinery's ECX because ECX stays live as `this`
// here for the +0x4C read that IsRefinery does not have. `cmp cl, [eax+4Ch]` puts `this` on
// the left, so the source order is `this->bPlayer == pHarvester->bPlayer`: equality operand
// order survives into the cmp.
BOOL CUnit::IsTargetRefinery(CUnit *pHarvester) {
  return (this->dwFlags & 1) &&
         (this->wType == 0x6E || this->wType == 0x9D || this->wType == 0x95) &&
         this->bPlayer == pHarvester->bPlayer;
}


// CUnit::IsMineralSrc -- 0x424380, 33 B. The WORD unit-type range test, no frame, 1/0
// return. Two things are fixed by the bytes.
//
// The field is read twice in source and not hoisted into a named local. A named local folds
// the two-bound range test into a biased subtract (`add eax, -0B0h / cmp eax, 2h`), a
// different three-instruction shape; reading the field on both sides leaves the two
// compares standing and lets MSVC CSE only the load.
//
// The constant is materialised into a register and the source cannot ask for that -- it is
// what MSVC does for a 16-bit compare whose immediate exceeds a signed imm8. Compare
// CUnit::IsRefinery below, where the same source shape emits `cmp eax, 6Eh` for the one
// constant that fits. Do not chase it with casts.
//
// Operand order confirms the sense: `cmp cx, ax` (constant first) with jnbe is
// `wType >= 0xB0`, and `cmp ax, cx` with jnbe is `wType <= 0xB2`.
BOOL CUnit::IsMineralSrc() {
  return 0xB0 <= this->wType && this->wType <= 0xB2;
}


// CUnit::IsTargetMineralSrc -- 0x4243B0, 37 B. IsMineralSrc's body with one unread stack
// parameter: byte-for-byte identical except `ret 4h` for `ret`, which pushes both jnbe
// displacements out by two. The parameter is never read, and declaring it away would change
// the epilogue, so the oracle's arity is the contract.
BOOL CUnit::IsTargetMineralSrc(CUnit *pHarvester) {
  return 0xB0 <= this->wType && this->wType <= 0xB2;
}


BOOL CUnit::IsNeutralResource() {
  return this->wType == 0xB0 || this->wType == 0xB1 || this->wType == 0xB2 ||
         this->wType == 0xBC;
}


// IsTypeNeutralResource -- 0x424420, 49 B. The free __fastcall predicate: the same four
// constants as the member CUnit::IsNeutralResource in the same source order, but the type
// arrives in CX and the constants go to EAX instead of the other way round -- the register
// assignment follows where the value came from, not anything in the source.
BOOL __fastcall IsTypeNeutralResource(WORD wUnitType) {
  return wUnitType == 0xB0 || wUnitType == 0xB1 || wUnitType == 0xB2 ||
         wUnitType == 0xBC;
}


// CUnit::IsResourceDest -- 0x424460, 94 B. One boolean expression, and only the branch
// targets settle its structure: two wrong readings both compiled, one at the oracle's
// exact size, so a small delta count is no evidence of the right program here.
//
// The displacements say that `dwFlags & 1` set short-circuits the disjunction true, as do
// wType == 0x85 and wType == 0x84, while wType != 0x83 goes to the zero -- so 0x83 falls
// into the IsZergBldgUpgrading call and the rest is a separate conjunct:
//   (flag || t==0x85 || t==0x84 || (t==0x83 && upgrading)) && (flag2) && (table)
// which reads as a completed depot, or a Lair or Hive, or a Hatchery mid-upgrade.
//
// The `mov eax, 1h` / `xor eax, eax` pair is why this is one expression and not a guard
// chain: as four `if (...) return 0;` statements plus `return 1;`, MSVC canonicalises the
// last bit test into a branchless mov/shr/and and comes out five bytes short. A
// multi-term conjunction's value is not any single bit, so MSVC must branch, and the
// merged zero then has a block to sit after, which is what puts it at the end.
BOOL CUnit::IsResourceDest() {
  return ((this->dwFlags & 1) || this->wType == 0x85 || this->wType == 0x84 ||
          (this->wType == 0x83 && this->IsZergBldgUpgrading())) &&
         (this->dwFlags & 2) && (gulUnitTypeFlags[this->wType] & 0x1000);
}


// 0x4244C0 -- CUnit::sub_4244C0, 133 B. A ten-term guard chain, every failure forward to
// one shared `pop edi / pop esi / ret`. A void function with one epilogue makes the
// guard-chain and the positively-wrapped spellings converge -- there is no separate zero to
// place -- so the chain is the plain reading of ten independent forward branches.
//
// The two-value order test is `!= && !=` in that order: `cmp al, 56h / jz` to the far side
// of the second compare, then `cmp al, 52h / jnz` to the failure, is exactly
// `if (bOrderId != 0x56 && bOrderId != 0x52) return;`, the first term's success
// short-circuiting the second. 0x56 before 0x52 because an || or && chain preserves source
// order where a switch would sort.
//
// `test dword ptr [esi+0DCh], 400h` is a DWORD test and not the narrowed `test byte` the
// `& 1` bodies above show, 0x400 being outside the low byte, so MSVC has no byte form to
// narrow to. The BOOL sub_4240D0 returns is discarded, as the plain ret implies.
void CUnit::sub_4244C0() {
  CUnit *pResource = this->pUnionD0;
  if (this->dwFlags & 0x400)
    return;
  if (this->bLockdownTimer)
    return;
  if (this->bStasisTimer)
    return;
  if (this->bParalyzeTimer)
    return;
  if (!pResource)
    return;
  if (!(gulUnitTypeFlags[pResource->wType] & 0x2000))
    return;
  if (this->bUnionCE)
    return;
  if (pResource->bUnionD3)
    return;
  if (this->bOrderId != 0x56 && this->bOrderId != 0x52)
    return;
  if (this->bOrderState != 2)
    return;
  pResource->RemoveHarvestUnit(this);
  this->RemoveOrder(0x59);
  this->sub_4240D0(pResource);
}


// CUnit::IsHarvestOrderQueued -- 0x424550, 56 B. An || chain over a call result, and the
// chain's order is proven: the nine constants come out 4F 51 52 53 55 56 57 59 58, with
// 0x59 before 0x58. A switch would have sorted them or built a table; an || chain keeps the
// order the source wrote. That one transposed pair is the whole evidence that this is an
// expression and not a switch, and it is why the last two arms must stay in that order.
//
// `cmp dword ptr [ecx+74h], 0h` rather than a test: MSVC cannot spell `test m, m`, so a
// memory-operand truth test against a pointer becomes a compare against 0.
// GetFirstQueuedOrder is called with ECX unreloaded, so on `this`, and returns a BYTE --
// only AL is compared.
BOOL CUnit::IsHarvestOrderQueued() {
  if (this->pOrderQueueHead == 0)
    return 0;
  order_id_t bOrder = this->GetFirstQueuedOrder();
  return bOrder == 0x4F || bOrder == 0x51 || bOrder == 0x52 || bOrder == 0x53 ||
         bOrder == 0x55 || bOrder == 0x56 || bOrder == 0x57 || bOrder == 0x59 ||
         bOrder == 0x58;
}


// Finds where a worker leaving a refinery reappears: on the side facing its
// nearest depot, else anywhere near it. Writes the spot and the facing.
BOOL __fastcall sub_424590(CUnit *pUnit, CUnit *pGas, mpoint_t *pOut,
                           BYTE *pDirection) {
  mpoint_t  from;
  mpoint_t  to;
  mrect_t r;  // Hyperion: a box of map positions
  CUnit *pDepot = pUnit->sub_431F00();
  if (pDepot && pGas) {
    from.x = pGas->pSprite->wPosX;
    from.y = pGas->pSprite->wPosY;
    to.x = pDepot->pSprite->wPosX;
    to.y = pDepot->pSprite->wPosY;
    BYTE bDir = (BYTE)AngleBetween(from.x, from.y, to.x, to.y);
    *pDirection = bDir;
    bDir -= 0x40;
    int dx = DIRECTIONS[bDir * 2];
    int dy = DIRECTIONS[bDir * 2 + 1];
    from.y += (dy << 5) >> 8;
    to.y += (dy << 5) >> 8;
    from.x += (dx << 5) >> 8;
    to.x += (dx << 5) >> 8;
    upos_t wY = pGas->pSprite->wPosY;
    upos_t wX = pGas->pSprite->wPosX;
    r.top = wY - gUnitBoundBox[pGas->wType].top;
    r.bottom = gUnitBoundBox[pGas->wType].bottom + wY;
    r.left = wX - gUnitBoundBox[pGas->wType].left;
    r.right = gUnitBoundBox[pGas->wType].right + wX;
    r.left -= gUnitBoundBox[pUnit->wType].right + 1;
    r.right += gUnitBoundBox[pUnit->wType].left + 1;
    r.top -= gUnitBoundBox[pUnit->wType].bottom + 1;
    r.bottom += gUnitBoundBox[pUnit->wType].top + 1;
    LineClip(&from, &to, &r);
    if (FindPlaceForUnit(pUnit, &to, pOut, &r, 1, 0x8000))
      return TRUE;
    return FindPlaceForUnit(pUnit, &to, pOut, NULL, 1, 0x8000) != 0;
  }
  from.x = pUnit->pSprite->wPosX;
  from.y = pUnit->pSprite->wPosY;
  return FindPlaceForUnit(pUnit, &from, pOut, NULL, 1, 0) != 0;
}


// CUnit::RemoveHarvestUnit -- 0x424750, 94 B. The doubly-linked unlink with a stack
// parameter: `this` is the resource, which owns the head at +0xD4, and the argument is the
// worker leaving. The head-against-prev asymmetry is the shape: when the worker has no prev
// the write goes to `this`->+0xD4, the head, otherwise to prev->+0xD8. MSVC emits that as
// an if/else with a jmp joining, exactly as written.
void CUnit::RemoveHarvestUnit(CUnit *pHarvester) {
  if (pHarvester->pUnionD4_HarvestPrev)
    pHarvester->pUnionD4_HarvestPrev->pUnionD8_HarvestNext =
        pHarvester->pUnionD8_HarvestNext;
  else
    this->pUnionD4_HarvestPrev = pHarvester->pUnionD8_HarvestNext;
  if (pHarvester->pUnionD8_HarvestNext)
    pHarvester->pUnionD8_HarvestNext->pUnionD4_HarvestPrev =
        pHarvester->pUnionD4_HarvestPrev;
  pHarvester->pUnionD4_HarvestPrev = 0;
  pHarvester->pUnionD8_HarvestNext = 0;
  pHarvester->pUnionD0 = 0;
}


// CUnit::UpdateResourceAnim -- 0x4247B0, 85 B. A threshold ladder into a byte local, and
// the bottom rung is an sbb the source cannot write: `sbb al, al / add al, K` is MSVC's
// materialisation of a two-constant ternary whose arms differ by exactly one, sbb making AL
// 0xFF or 0x00 from the carry and the add biasing it. Written as an if/else over two
// assignments it comes out as a branch instead, so the bottom rung must be a ternary while
// the two above it are if/else-if.
//
// Note the operand order against IsMineralSrc: here every `>=` emits
// `cmp <amount>, <const>` with jb, so the source writes `amount >= K`, where in
// IsMineralSrc the same operator wanted `cmp <const>, <type>` with ja, which needs
// `K <= type`. Both encodings are reachable and the source order picks which, flipping the
// branch mnemonic with it. Do not chase either with a cast.
//
// The frame slot exists because the value is both compared against +0xD2 and passed; a
// plain local produces it.
void CUnit::UpdateResourceAnim() {
  BYTE bAnim;
  if (this->wResourceAmount >= 0x2EE)
    bAnim = 0x14;
  else if (this->wResourceAmount >= 0x1F4)
    bAnim = 0x0F;
  else
    bAnim = this->wResourceAmount >= 0xFA ? 0x0E : 0x0D;
  if (bAnim != this->bResourceAnim) {
    this->bResourceAnim = bAnim;
    this->pSprite->SetAnimation(bAnim, 1);
  }
}
