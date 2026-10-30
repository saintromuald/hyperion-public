// CUnitBuild.cpp -- the build and rally block: what a unit under construction does each
// frame (the build-time countdown, the hp gain, the percent-complete query), the rally
// point a production building keeps and the three bodies that set and draw it, the build
// queue accessor, the five unit-type predicates the rest of the engine asks about
// carriers, reavers, fighters and zerg townhalls, and the placement clamp. Bodies are in
// ascending oracle VA order, which under /Gy and /Ox is 1.17's own declaration order --
// note that it puts ModifyLife (0x41B900) before SetLife (0x41BB50), whose tail call it is.
//
// Two membership dependencies, both forbidding an inline, so the danger here is adding a
// body rather than removing one. CUnit::EndUninterruptableDispatchOrder must stay out of
// this TU, or DispatchUndoFlyer's tail jmp to it becomes an inline and the body grows; and
// CUnit::SetLife must stay in it, at a size /Ob2 will not splice (111 B against
// ModifyLife's 15). Guard a tail-call pair together or not at all.

#include "oc_data.h"  // Hyperion: OC_VA
#include "../h/playertables.h"  // Hyperion: player slots past 11
#include "platform.h"
#include "CUnit.h"
#include "CSprite.h"
#include "CImage.h"      // CUnit::SetLife dereferences pMainImage->wImageID
#include "rect16.h"      // ours: gUnitBoundBox's element type
#include "CUnitBuild.h"  // ours: ClipToMap, and point16_t with it
#include "rclick.h"      // ours: show_target_x
#include "TAI_AreaStrength.h"  // ours: tai_assign_unit_strength
#include "CTarget.h"
#include "CUnitHarvest.h"
#include "SAI_Build.h"
#include "SAI_Banker.h"
#include "banker.h"
#include "CUnitInit.h"
#include "collide.h"
#include "gamesnd.h"
#include "point16.h"
#include "CUnitZBuild.h"
#include "CUnitTerran.h"
#include "placebox.h"     // sub_47DB60, set_placebox, town_move_morphed_building
#include "CUnitZSpells.h"
#include "CFlingy.h"       // AngleBetween, SetDestination, AtDestination
#include "TrigAct.h"       // net_rand
#include "CUnitTSpells.h"  // net_randbetween
#include "../h/handicap.h"  // Hyperion: a player's handicap

// The globals this TU reaches, dual-spelled, one #ifndef per name: the plain extern arm
// alone leaves the shipped form's baked slots unchecked, and one guard over the whole
// block would go false and undeclare every name under it. Every name is the sheet's or the
// registry's. Two extents are exact, bracketed by the sheet's next name -- gUnitBoundBox
// 0x522C38, 228 * 8 (rect16_t), and guwUnitTimeToBuild 0x5238B0, 228 * 2 (WORD) -- while
// gubUnitPriorityPlane 0x520A98 has a 232-byte storage ceiling and units.dat's 228 rows.
#ifdef OC_DLL_MODE
  #ifndef glCheatBits
  #define glCheatBits (*reinterpret_cast<LONG *>(OC_VA(0x5432FCu)))
  #endif
  #ifndef gxUnitLife
  #define gxUnitLife (reinterpret_cast<LONG *>(OC_VA(0x520708u)))
  #endif
  #ifndef guwUnitTimeToBuild
  #define guwUnitTimeToBuild (reinterpret_cast<WORD *>(OC_VA(0x5238B0u)))
  #endif
  #ifndef gdwLocalMask
  #define gdwLocalMask (*reinterpret_cast<DWORD *>(OC_VA(0x672668u)))
  #endif
  #ifndef img_dmgOverlay
  #define img_dmgOverlay (reinterpret_cast<DWORD *>(OC_VA(0x516D38u)))
  #endif
  #ifndef gUnitBoundBox
  #define gUnitBoundBox (reinterpret_cast<rect16_t *>(OC_VA(0x522C38u)))
  #endif
  #ifndef gwMapPixWidth
  #define gwMapPixWidth (*reinterpret_cast<WORD *>(OC_VA(0x543230u)))
  #endif
  #ifndef gwMapPixHeight
  #define gwMapPixHeight (*reinterpret_cast<WORD *>(OC_VA(0x543234u)))
  #endif
  #ifndef gubUnitPriorityPlane
  #define gubUnitPriorityPlane (reinterpret_cast<BYTE *>(OC_VA(0x520A98u)))
  #endif
  #ifndef gubUnitOrderGuard
  #define gubUnitOrderGuard (reinterpret_cast<BYTE *>(OC_VA(0x520F20u)))
  #endif
  #ifndef gulUnitTypeFlags
  #define gulUnitTypeFlags (reinterpret_cast<DWORD *>(OC_VA(0x521660u)))
  #endif
  #ifndef gUnitPlaceboxSize
  #define gUnitPlaceboxSize (reinterpret_cast<point16_t *>(OC_VA(0x522728u)))
  #endif
  #ifndef guwUnitMineralsCost
  #define guwUnitMineralsCost (reinterpret_cast<WORD *>(OC_VA(0x523520u)))
  #endif
  #ifndef guwUnitGasesCost
  #define guwUnitGasesCost (reinterpret_cast<WORD *>(OC_VA(0x5236E8u)))
  #endif
  #ifndef gubUnitFlingy
  #define gubUnitFlingy (reinterpret_cast<BYTE *>(OC_VA(0x51FAA8u)))
  #endif
  #ifndef guwFlingySprite
  #define guwFlingySprite (reinterpret_cast<WORD *>(OC_VA(0x510AE0u)))
  #endif
  #ifndef guwSpriteImage
  #define guwSpriteImage (reinterpret_cast<WORD *>(OC_VA(0x51EAA8u)))
  #endif
  #ifndef gPlayerMinerals
  #define gPlayerMinerals (reinterpret_cast<int *>(OC_VA(0x5263D0u)))
  #endif
  #ifndef gPlayerGas
  #define gPlayerGas (reinterpret_cast<int *>(OC_VA(0x526400u)))
  #endif
  #ifndef byte_5264C2
  #define byte_5264C2 (*reinterpret_cast<BYTE *>(OC_VA(0x5264C2u)))
  #endif
#else
extern LONG     glCheatBits;
extern LONG     gxUnitLife[228] OC_UNALIGNED;
extern WORD     guwUnitTimeToBuild[228] OC_UNALIGNED;
extern DWORD    gdwLocalMask;
extern DWORD    img_dmgOverlay[999];
extern rect16_t gUnitBoundBox[228] OC_UNALIGNED;
extern upos_t   gwMapPixWidth;
extern upos_t   gwMapPixHeight;
extern BYTE     gubUnitPriorityPlane[228] OC_UNALIGNED;
extern order_id_t gubUnitOrderGuard[228] OC_UNALIGNED;
extern DWORD    gulUnitTypeFlags[228] OC_UNALIGNED;
extern point16_t gUnitPlaceboxSize[228] OC_UNALIGNED;
extern WORD     guwUnitMineralsCost[228] OC_UNALIGNED;
extern WORD     guwUnitGasesCost[228] OC_UNALIGNED;
extern flingy_id_t gubUnitFlingy[228] OC_UNALIGNED;
extern WORD     guwFlingySprite[209] OC_UNALIGNED;
extern WORD     guwSpriteImage[517];
extern PlayerMineralsTable gPlayerMinerals;
extern PlayerGasTable gPlayerGas;
extern BYTE     byte_5264C2;
#endif

#ifndef wpn_attackangle_g
#ifdef OC_DLL_MODE
#define wpn_attackangle_g (reinterpret_cast<BYTE *>(OC_VA(0x525FA0u)))
#else
extern BYTE wpn_attackangle_g[];
#endif
#endif

// CUnit::AtMoveTarget
//
// This returns 0, 1 or 2 while its declaration says BOOL: the tail is
// `and eax, 80000h / neg / sbb / neg / inc`, and CUnit::DispatchHarvest compares the
// result against 2. BOOL is a typedef for int, so the two spellings are the same code.
//
// Source it as `1 + (flag != 0)`, not as a ternary yielding 2 or 1: a ternary emits a
// branch here instead of the branchless neg/sbb/neg triple. The guard is one && with the
// failure block last, where two separate `if (...) return 0;` statements place the zero
// exit inline after the first compare. The Y compare needs a cast and the X compare does
// not: wMoveTargetY is WORD and CSprite::wPosY is short, so that pair promotes to int and
// compares at 32 bits, while wMoveTargetX and wPosX are both WORD and compare at 16.
BOOL CUnit::AtMoveTarget() {
  CSprite *pSprite = this->pSprite;

  if (this->wMoveTargetX == pSprite->wPosX &&
      this->wMoveTargetY == PosU(pSprite->wPosY))
    return (this->dwFlags & 0x80000) ? 2 : 1;

  return 0;
}

// CUnit::Build
// Adds nHpGain to lHitPoints as a full dword; bTrainingUnit picks sub_424DB0 over
// FinishedMaking (DispatchConstructBuilding passes 0, DispatchTrain 1). It returns 1 except
// when sub_424DB0 fails, and DispatchTrain tests it. CUnit::CountDownToCompletion is inlined,
// so it must stay in this TU.
BOOL CUnit::Build(int nHpGain, BOOL bTrainingUnit) {
  this->SetLife(this->lHitPoints + nHpGain);

  if (!this->CountDownToCompletion())
    return 1;

  if (bTrainingUnit) {
    if (!this->sub_424DB0())
      return 0;
  } else {
    this->FinishedMaking();
  }

  if (!(this->dwFlags & 2)) {
    gamesnd_create_man(this);
  } else if (this->GetRace() == 1) {
    CUnit *pAddon = this->sub_431AE0();
    if (pAddon != NULL)
      this->sub_4359C0(pAddon);
  }

  return 1;
}

// Cancels construction or a morph in progress and refunds it.
void CUnit::CancelBuild() {
  if (IsDeadOrDying())
    return;
  DWORD dwUnitFlags = dwFlags;
  if (dwUnitFlags & 1)
    return;
  int nType = wType;
  if (nType == 0x2C || nType == 0x67 || nType == 0x3E)
    return;
  if ((WORD)nType == 0x86 && pUnionD0 != NULL)
    return;
  if (nType == 0x2B || nType == 0x26)
    return;
  if (dwUnitFlags & 2) {
    if (GetRace() == 0) {
      sub_43B380(reinterpret_cast<intptr_t>(this));
      return;
    }
    refund_unit_partial(bPlayer, wType);
  } else if (nType == 0x24 || nType == 0x3B || nType == 0x61) {
    bldg_add_resources(bPlayer, wBuildQueue[bBuildQueueSlot % 5]);
  } else {
    bldg_add_resources(bPlayer, nType);
  }
  if (wType == 0x3B || wType == 0x61) {
    if (wType == 0x3B)
      TransformType(0x2B);
    else
      TransformType(0x26);
    wRemainingBuildTime = 0;
    wBuildQueue[bBuildQueueSlot] = 0xE4;
    pSprite->ChangeImage(
        guwSpriteImage[guwFlingySprite[gubUnitFlingy[wPrevUnitType]]], 0);
    bOrderFlags &= ~4;
    pSprite->SetAnimation(0x0E, 1);
    SetOrder(0x29);
    return;
  }
  if (wType == 0x0E) {
    if (pUnion80 != NULL) {
      pUnion80->pUnionD0 = NULL;
      pUnion80->bOrderState = 0;
    }
    status_set_update();
  }
  Suicide();
}

// ClipToMap
// Clamps a position to the playable map allowing for the unit type's own extents.
// __fastcall, both parameters in registers; the x clamp is an if/else-if. The axes are not
// symmetric and must stay so: y subtracts a further 0x20 in the test and 0x21 in the store
// where x subtracts only 1, so one helper for both axes changes the code.
void ClipToMap(mpoint_t *pPos, WORD wType) {
  if (pPos->x < gUnitBoundBox[wType].left)
    pPos->x = gUnitBoundBox[wType].left;
  else if (pPos->x >= gwMapPixWidth - gUnitBoundBox[wType].right)
    pPos->x = PosS(gwMapPixWidth - gUnitBoundBox[wType].right - 1);

  if (pPos->y < gUnitBoundBox[wType].top)
    pPos->y = gUnitBoundBox[wType].top;
  else if (pPos->y >= gwMapPixHeight - gUnitBoundBox[wType].bottom - 32)
    pPos->y = PosS(gwMapPixHeight - gUnitBoundBox[wType].bottom - 33);
}

// CUnit::CountDownToCompletion
//
// Ticks wRemainingBuildTime down by 1, or by 16 under glCheatBits bit 1, clamping at zero.
// It returns 1 only on the already-zero entry path: the answer is "was it already
// finished", not "did it finish".
//
// Two things are part of the code and neither is the arithmetic. The x16 arm is the
// fall-through and the x1 arm the jump target, the reverse of how the condition reads, so
// the source tests the flag positively and inverting it swaps the blocks. And
// `add eax, -10h` is int arithmetic on a widened value, not a 16-bit wrap -- a WORD local
// emits `add eax, 0FFF0h` -- so the value lives in an int between the movzx and the
// truncating store back, and `test ax, ax / jns` is a signed 16-bit test of the stored
// half, so the clamp catches an underflow the int arithmetic let through. The
// first zero test is `test ax, ax`, so the early return reads the WORD field and not the
// widened local.
BOOL CUnit::CountDownToCompletion() {
  if (this->wRemainingBuildTime == 0)
    return 1;

  int nLeft = this->wRemainingBuildTime;

  if (glCheatBits & 2) {
    nLeft -= 16;
    this->wRemainingBuildTime = PosU(nLeft);
    if ((short)this->wRemainingBuildTime < 0)
      this->wRemainingBuildTime = 0;
  } else {
    nLeft -= 1;
    this->wRemainingBuildTime = PosU(nLeft);
  }

  return 0;
}

// 0x41A580 (482 B). Walks to the build site, then places the queued building
// and hands the worker over to it. The two gotos keep the XYInRadius
// arguments loaded after the nState test; one `||` chain hoists them.
void CUnit::DispatchBuild() {
  if (dwUnion134 != 0)
    dwFlags |= 0x800000;
  switch (bOrderState) {
  case 1: {
    int nState = AtMoveTarget();
    if (nState == 0)
      return;
    if (nState == 2)
      goto end;
    {
      int nY = wOrderTargetY;
      int nX = wOrderTargetX;
      if (!reinterpret_cast<CTarget *>(this)->XYInRadius(0x80, nX, nY))
        goto end;
    }
    if (!sub_41BCB0()) {
end:
      EndDispatchOrder();
      return;
    }
    dwFlags &= ~0x100000;
    pSprite->bElevationLevel = gubUnitPriorityPlane[wType] + 1;
    if (pOrderQueueHead != NULL) {
      PrependOrder(0x96, pOrderQueueHead);
    } else {
      AppendOrder(0x96, NULL);
      AppendOrder(gubUnitOrderGuard[wType], NULL);
    }
    CUnit *pBuilt;
    int nType = wBuildQueue[bBuildQueueSlot];
    if (nType == 0x6E)
      pBuilt = ReplaceGeyser(nType);
    else
      pBuilt = CreateUnitAt(nType, wOrderTargetX, wOrderTargetY, bPlayer);
    wBuildQueue[bBuildQueueSlot] = 0xE4;
    if (pBuilt == NULL) {
      ShowCreateError(bPlayer);
      SetOrder(gubUnitOrderGuard[wType]);
      return;
    }
    pBuilt->pUnion80 = this;
    bOrderId = 0x21;
    bOrderState = 3;
    pOrderTarget = pBuilt;
    if (placebox_is_on() && !sub_47DB60())
      set_placebox(0, NULL, 1);
    status_set_update();
    pBuilt->SetOrder(0x2C);
    town_move_morphed_building(this, pBuilt);
    return;
  }
  case 0:
    status_set_update();
    if (pOrderTarget != NULL) {
      wOrderTargetX = pOrderTarget->pSprite->wPosX;
      wOrderTargetY = pOrderTarget->pSprite->wPosY;
    }
    if (SetDestination_xy(wOrderTargetX, wOrderTargetY))
      bOrderState = 1;
    return;
  }
}

// CUnit::DispatchBuildSelf
// A 7-arm jump table on bOrderState, walking a building through its construction animation
// at 1/5, 2/5, 3/5 and 4/5 of the type's max hit points. The (DWORD) cast makes the `/5`
// unsigned and the (int) cast back keeps the compare signed. The x1..x4 multipliers are
// written out, each with its own encoding, where a loop or a table would not be.
void CUnit::DispatchBuildSelf() {
  switch (this->bOrderState) {
    case 0:
      if (this->lHitPoints > (int)((DWORD)HandicapLife(gxUnitLife[this->wType], this->bPlayer) / 5))
        this->bOrderState = 1;
      break;

    case 1:
      this->pSprite->SetAnimation(0x0D, 1);
      this->bOrderState = 2;
      break;

    case 2:
      if (this->lHitPoints > (int)((DWORD)HandicapLife(gxUnitLife[this->wType], this->bPlayer) * 2 / 5))
        this->bOrderState = 3;
      break;

    case 3:
      this->pSprite->SetAnimation(0x0E, 1);
      this->bOrderState = 4;
      break;

    case 4:
      if (this->lHitPoints > (int)((DWORD)HandicapLife(gxUnitLife[this->wType], this->bPlayer) * 3 / 5))
        this->bOrderState = 5;
      break;

    case 5:
      this->SyncConstructionGraphic(0);
      this->pSprite->SetAnimation(0x0F, 1);
      this->bOrderState = 6;
      break;

    case 6:
      if (this->lHitPoints > (int)((DWORD)HandicapLife(gxUnitLife[this->wType], this->bPlayer) * 4 / 5))
        this->SetOrder(0x17);
      break;
  }
}

// SCV construction: walk to the site and claim it, then wander to random points
// around the building while it builds, and hand off when it completes.
// Case 4's two boxes are rect16_t locals and read the site type at each use;
// the gain is a WORD local.
void CUnit::DispatchConstructBuilding() {
  CUnit *pTarget = pOrderTarget;
  if (pTarget != NULL && !(pTarget->dwFlags & 1)) {
    switch (bOrderState) {
    case 0:
      if (!SetDestination_CUnit(pTarget))
        return;
      bOrderState = 2;
    case 2: {
      int nState = AtMoveTarget();
      if (nState == 0)
        return;
      if (nState == 2) {
        EndDispatchOrder();
        return;
      }
      status_set_update();
      CUnit *pBuilder = pTarget->pUnion80;
      if (pBuilder != NULL && pBuilder != this && pBuilder->pOrderTarget == pTarget) {
        EndDispatchOrder();
        return;
      }
      pTarget->pUnion80 = this;
      dwFlags &= ~0x100000;
      pSprite->bElevationLevel = gubUnitPriorityPlane[wType] + 1;
      SetOrder_interrupt(0x96, 0);
      SetOrder_interrupt(gubUnitOrderGuard[wType], 0);
      CSprite *pSite = pTarget->pSprite;
      upos_t wY = pSite->wPosY;
      upos_t wX = pSite->wPosX;
      if (SetDestination_xy(PosS(wX), PosS(wY)))
        bOrderState = 3;
      return;
    }
    case 3:
      if (AtMoveTarget())
        bOrderState = 4;
      return;
    case 4: {
      CSprite *pSite = pTarget->pSprite;
      mrect_t  rcOuter;
      mrect_t  rcBox;
      // Hyperion: a site past 255 a side by Hyperion's rules
      rcOuter.left = pSite->wPosX - (SpriteGrpWidth(pSite) >> 1);
      rcOuter.right = rcOuter.left + SpriteGrpWidth(pSite) - 1;
      rcOuter.top = pSite->wPosY - (SpriteGrpHeight(pSite) >> 1);
      rcOuter.bottom = rcOuter.top + SpriteGrpHeight(pSite) - 1;
      rcBox.left = pSite->wPosX - gUnitPlaceboxSize[pTarget->wType].x / 2;
      rcBox.right = rcBox.left + gUnitPlaceboxSize[pTarget->wType].x - 1;
      rcBox.top = pSite->wPosY - gUnitPlaceboxSize[pTarget->wType].y / 2;
      rcBox.bottom = rcBox.top + gUnitPlaceboxSize[pTarget->wType].y - 1;
      rcBox.left += gUnitBoundBox[wType].left + 1;
      rcBox.right -= gUnitBoundBox[wType].right + 1;
      rcBox.top += gUnitBoundBox[wType].top + 1;
      rcBox.bottom -= gUnitBoundBox[wType].bottom + 1;
      if (rcBox.left < rcOuter.left)
        rcBox.left = rcOuter.left;
      if (rcBox.top < rcOuter.top)
        rcBox.top = rcOuter.top;
      if (rcBox.right > rcOuter.right)
        rcBox.right = rcOuter.right;
      if (rcBox.bottom > rcOuter.bottom)
        rcBox.bottom = rcOuter.bottom;
      wOrderTargetX = PosS(net_randbetween(rcOuter.left, rcOuter.right, 9));
      wOrderTargetY = PosS(net_randbetween(rcOuter.top, rcOuter.bottom, 9));
      ClipToMap(&orderTargetPos, wType);
      upos_t wY = pSprite->wPosY;
      upos_t wX = pSprite->wPosX;
      if (sub_42DA50(wOrderTargetX - PosS(wX), wOrderTargetY - PosS(wY))) {
        wOrderTargetX = PosS(net_randbetween(rcBox.left, rcBox.right, 9));
        wOrderTargetY = PosS(net_randbetween(rcBox.top, rcBox.bottom, 9));
        ClipToMap(&orderTargetPos, wType);
      }
      if (!SetDestination_xy(wOrderTargetX, wOrderTargetY))
        return;
      bOrderState = 5;
      break;
    }
    case 5:
      bOrderState = 6;
      break;
    case 6: {
      int nY = wOrderTargetY;
      int nX = wOrderTargetX;
      if (reinterpret_cast<CTarget *>(this)->XYInRadius(0x14, nX, nY)) {
        Stop();
        reinterpret_cast<CFlingy *>(this)->SetDestination(
            PosS(pOrderTarget->pSprite->wPosX), pOrderTarget->pSprite->wPosY);
        upos_t wY = pSprite->wPosY;
        upos_t wX = pSprite->wPosX;
        const short pDest[2] = {PosS(wDestinationX), PosS(wDestinationY)};  // Hyperion: the destination by name (stage E)
        int nDx, nDy;
        reinterpret_cast<CFlingy *>(this)->GetVectorComponents(
            AngleBetween(PosS(wX), PosS(wY), pDest[0], pDest[1]), 0x14, &nDx, &nDy);
        wOrderTargetX = pSprite->wPosX + nDx;
        wOrderTargetY = pSprite->wPosY + nDy;
        bOrderState = 7;
      }
      break;
    }
    case 7:
      if (!reinterpret_cast<CFlingy *>(this)->AtDestination()) {
        BYTE bDir = bUnion21;
        upos_t wY = pSprite->wPosY;
        upos_t wX = pSprite->wPosX;
        const short pDest[2] = {PosS(wDestinationX), PosS(wDestinationY)};  // Hyperion: the destination by name (stage E)
        int nDiff = bDir - AngleBetween(PosS(wX), PosS(wY), pDest[0], pDest[1]);
        if (nDiff < 0)
          nDiff += 0x100;
        if (nDiff > 0x80)
          nDiff = 0x100 - nDiff;
        if (nDiff > wpn_attackangle_g[GetWeaponGndEx()])
          break;
      }
      bUnion54 = (BYTE)((net_rand(10) & 0x3F) + 0x1E);
      pSprite->SetAnimation(0x0F, 1);
      bOrderState = 8;
      break;
    case 8:
      if (bUnion54 == 0) {
        pSprite->SetAnimation(0x08, 1);
        bOrderState = 4;
      }
      break;
    }
    // Merged arms under case 8's bUnion54 test; with case 4's type reads they give
    // pTarget ESI.
    WORD wGain; if (bUnion54 == 0) wGain = pTarget->wHPGain; else wGain = pTarget->wHPGain;
    if (glCheatBits & 2)
      wGain <<= 4;
    pTarget->Build(wGain, 0);
    if (!(pTarget->dwFlags & 1))
      return;
  }
  pSprite->SetAnimation(0x0C, 1);
  if (pTarget != NULL) {
    pTarget->pUnion80 = NULL;
    gamesnd_build_complete(this);
    if (pTarget->IsTargetRefinery(this) || pTarget->IsTargetMineralSrc(this)) {
      SetOrder_CUnit(0x4F, pTarget);
      return;
    }
  }
  EndDispatchOrder();
}

// SCV repair: walk to a damaged Terran target, face it, then pay and heal one
// step at a time. The two SetAnimation(0x0C) exits are written out separately.
void CUnit::DispatchRepair() {
  CUnit *pTarget = pOrderTarget;
  if (pTarget != NULL && pTarget->lHitPoints < HandicapLife(gxUnitLife[pTarget->wType], pTarget->bPlayer) && // Hyperion: at the owner's handicap
      pTarget->bStasisTimer == 0 &&
      (dwUnion134 == 0 || (pTarget->dwFlags & 0x20000) == 0 ||
       pTarget->pOrderTarget == this) &&
      (pTarget->dwFlags & 0x40) == 0 &&
      (gulUnitTypeFlags[pTarget->wType] & 0x40000000) != 0 &&
      (pTarget->dwFlags & 1) != 0) {
    if (pTarget->GetRace() != 1) {
      pSprite->SetAnimation(0x0C, 1);
      EndDispatchOrder();
      return;
    }
    switch (bOrderState) {
    case 0: {
      int nOre;
      int nGas;
      WORD wTime;
      sub_41B910(pTarget, &nOre, &nGas, &wTime);
      build_check_money(bPlayer, nOre, nGas);
      bOrderState = 1;
      *reinterpret_cast<WORD *>(_unmapped_CC) = 0;
    }
    case 1:
      if ((pTarget->dwFlags & 1) == 0) {
        SetOrder_CUnit(0x21, pTarget);
        if (placebox_is_on() && !sub_47DB60())
          set_placebox(0, NULL, 1);
        return;
      }
      if (SetDestination_CUnit(pTarget)) {
        reinterpret_cast<CTarget *>(this)->SetFocusTarget(pTarget);
        bOrderState = 6;
      }
      return;
    case 6:
      if (!AtMoveTarget()) {
        ApproachMoveTarget_CUnit(pTarget);
        return;
      }
      if (!IsWithinTargetRange(5, pTarget)) {
        bOrderState = 1;
        return;
      }
      reinterpret_cast<CFlingy *>(this)->SetDestination(
          PosS(pTarget->pSprite->wPosX), PosS(pTarget->pSprite->wPosY));
      reinterpret_cast<CTarget *>(this)->SetFocusTarget(pTarget);
      bOrderState = 7;
      break;
    case 7:
      if (!IsWithinTargetRange(5, pTarget)) {
        bOrderState = 1;
        return;
      }
      reinterpret_cast<CFlingy *>(this)->SetDestination(
          PosS(pTarget->pSprite->wPosX), PosS(pTarget->pSprite->wPosY));
      reinterpret_cast<CTarget *>(this)->SetFocusTarget(pTarget);
      if (!reinterpret_cast<CFlingy *>(this)->AtDestination()) {
        BYTE bDir = bUnion21;
        upos_t wY = pSprite->wPosY;
        upos_t wX = pSprite->wPosX;
        const short pDest[2] = {PosS(wDestinationX), PosS(wDestinationY)};  // Hyperion: the destination by name (stage E)
        int nDiff = bDir - AngleBetween(PosS(wX), PosS(wY), pDest[0], pDest[1]);
        if (nDiff < 0)
          nDiff += 0x100;
        if (nDiff > 0x80)
          nDiff = 0x100 - nDiff;
        if (nDiff > wpn_attackangle_g[GetWeaponGndEx()])
          break;
      }
      pSprite->SetAnimation(0x0F, 1);
      bOrderState = 8;
      break;
    case 8:
      reinterpret_cast<CFlingy *>(this)->SetDestination(
          PosS(pTarget->pSprite->wPosX), PosS(pTarget->pSprite->wPosY));
      reinterpret_cast<CTarget *>(this)->SetFocusTarget(pTarget);
      if (!IsWithinTargetRange(5, pTarget) || pTarget->bOrderId == 0x4A) {
        pSprite->SetAnimation(0x08, 1);
        bOrderState = 1;
      }
      break;
    }
    if ((*reinterpret_cast<WORD *>(_unmapped_CC))-- == 0 && !sub_41BAB0(pTarget)) {
      pSprite->SetAnimation(0x0C, 1);
      EndDispatchOrder();
      return;
    }
    WORD wGain = pTarget->wHPGain;
    if (glCheatBits & 2)
      wGain <<= 4;
    pTarget->SetLife(pTarget->lHitPoints + wGain);
    if (pTarget->lHitPoints < HandicapLife(gxUnitLife[pTarget->wType], pTarget->bPlayer))
      return;
  }
  pSprite->SetAnimation(0x0C, 1);
  EndDispatchOrder();
  if (pTarget != NULL && (pTarget->dwFlags & 0x40) == 0)
    pTarget->pUnion80 = NULL;
}

// Builds the head of the production queue; bUnionE2 is the training state.
void CUnit::DispatchTrain() {
  if (dwFlags & 0x400)
    return;
  if (bLockdownTimer != 0 || bStasisTimer != 0 || bParalyzeTimer != 0)
    return;
  if (GetRace() == 0 && wType != 0x82)
    return;
  switch (bUnionE2) {
  case 2: {
    CUnit *pBuilt = reinterpret_cast<CUnit *>(dwUnionEC);
    if (pBuilt != NULL) {
      if (pBuilt->Build(pBuilt->GetBuildHpGain(), 1)) {
        pBuilt = reinterpret_cast<CUnit *>(dwUnionEC);
        if (!(pBuilt->dwFlags & 1))
          return;
        register_unit_for_ai(this, pBuilt);
        pBuilt = reinterpret_cast<CUnit *>(dwUnionEC);
        if (pBuilt->wType == 0x0E)
          pBuilt->Hide();
        else
          pBuilt->RallyToFactoryTarget(this);
        bldg_clear_slot(this, bBuildQueueSlot);
        wBuildQueue[bBuildQueueSlot] = 0xE4;
        bBuildQueueSlot = (BYTE)(bBuildQueueSlot + 1) % 5;
      } else {
        RemoveBuildQueue(0);
      }
    }
    status_set_update();
    dwUnionEC = 0;
    bUnionE2 = 0;
    return;
  }
  case 0:
  case 1: {
    WORD wQueued = wBuildQueue[bBuildQueueSlot];
    if (wQueued == 0xE4) {
      SetBackgroundOrder(0x17);
      pSprite->SetAnimation(0x14, 1);
      return;
    }
    dwUnionEC = reinterpret_cast<DWORD_PTR>(sub_41BC30(wQueued, bUnionE2 == 0));
    if (dwUnionEC == 0) {
      bUnionE2 = 1;
      return;
    }
    bUnionE2 = 2;
    pSprite->SetAnimation(0x13, 1);
    return;
  }
  }
}

// CUnit::DispatchUndoFlyer
// SetOrder_interrupt sits inside `if (pUnion74 == NULL)` and the terminator runs
// unconditionally: not an else arm, not an early return. The flag edit is one expression on
// one load (clear bits 21 and 23, set bit 20). The tail jump to
// CUnit::EndUninterruptableDispatchOrder needs that callee to stay out of this TU.
void CUnit::DispatchUndoFlyer() {
  this->pSprite->bElevationLevel = gubUnitPriorityPlane[this->wType];
  this->dwFlags = (this->dwFlags & ~0xA00000) | 0x100000;

  this->UpdateUnitCollision();
  this->ResetMovementState();

  if (this->pUnion74 == NULL)
    this->SetOrder_interrupt(gubUnitOrderGuard[this->wType], 0);

  this->EndUninterruptableDispatchOrder();
}

// CUnit::DisplayRallyTarget
// Two peer early-return guards, not a nested pair. The rally pair is short and the position
// pair WORD: the WORD side is cast to short, which gives a 16-bit compare and builds the call
// argument with movsx; the other cast fixes the compare but builds it with movzx.
void CUnit::DisplayRallyTarget() {
  if (this->wRallyX == 0 && this->wRallyY == 0)
    return;

  if (this->wRallyX == PosS(this->wPositionX) &&
      this->wRallyY == PosS(this->wPositionY))
    return;

  show_target_x(this->wRallyX, this->wRallyY, 0);
}

// CUnit::GetBuildHpGain
//
// The per-frame hp increment a unit under construction gains: wHPGain as it stands, or x16
// under glCheatBits bit 1. The load is hoisted above the branch by the compiler and not by
// the source -- it is needed on both paths, and reading the field inside each arm or
// naming a local before the if all emit the same bytes.
//
// The return is WORD and this body cannot show it: both callers emit `movzx eax, ax` right
// after the call, which is unemittable for an int return. A WORD return lives in AX with
// the high half free, so the oracle leaves the shl result untruncated here and the caller
// extends it.
WORD CUnit::GetBuildHpGain() {
  int nGain = this->wHPGain;
  if (glCheatBits & 2)
    nGain <<= 4;
  return nGain;
}

// CUnit::GetBuildPercentage
// `(total - remaining) * 100 / total` against the units.dat build time. The divide is
// signed, so the arithmetic is in int. The merged type local is a WORD, and the four arms
// pick one variable feeding one divide; an early return or an if/else gives two divides.
int CUnit::GetBuildPercentage() {
  WORD wBuildType = this->wType;

  if (wBuildType == 0x24 || wBuildType == 0x3B || wBuildType == 0x61 ||
      this->IsZergBldgUpgrading())
    wBuildType = this->wBuildQueue[this->bBuildQueueSlot];

  int nTotal = guwUnitTimeToBuild[wBuildType];

  return (nTotal - this->wRemainingBuildTime) * 100 / nTotal;
}

// CUnit::GetBuildUnit
//
// Signed modulo by a register constant: `cdq / mov esi, 5h / idiv esi` is a real idiv, not
// the reciprocal multiply MSVC uses for `% 5` on an unsigned value it can prove
// non-negative, so the sum must be built in int and the movzx on the byte queue slot is
// that field widening into it. The return is 16-bit: `mov ax, ...` leaves the top half of
// EAX alone, and bf_stop_unit consumes it with a 16-bit compare.
WORD CUnit::GetBuildUnit(int nIndex) {
  return this->wBuildQueue[(this->bBuildQueueSlot + nIndex) % 5];
}

// CUnit::GetConstructionTarget
// Two four-armed disjunctions, not a nest. `return NULL` reuses IsConstructingAddon's zero
// in EAX, so the fall-through is the null path.
CUnit *CUnit::GetConstructionTarget() {
  if (this->bOrderId == 0x1E || this->bOrderId == 0x22 || this->bOrderId == 0x21 ||
      this->IsBuildingProtoss())
    return this->pOrderTarget;

  if (this->bBackgroundOrderId == 0x26 || this->bBackgroundOrderId == 0x3F ||
      this->IsConstructingAddon())
    return reinterpret_cast<CUnit *>(this->dwUnionEC);

  return NULL;
}

// The five unit-type predicates -- oracle 0x41B380, 0x41B3A0, 0x41B3D0, 0x41BF90 and
// 0x41BFB0. One shape, with the failure block last in all five (every jz jumps forward to
// a shared `mov eax, 1h`), which is what a single `return a || b;` emits; an explicit
// `if (...) return 1; return 0;` places the blocks the other way round. The compare
// encoding splits the family and the source cannot choose: the first four emit
// `cmp eax, imm8` while IsZergTownhall emits `mov ecx, K / cmp ax, cx` for all three of
// its constants, because 0x83, 0x84 and 0x85 exceed a signed imm8 and 0x48..0x55 do not.
// 0x41B380 (23 B) -- 0x48 Carrier, 0x52 Carrier (hero)
BOOL CUnit::IsCarrier() {
  return this->wType == 0x48 || this->wType == 0x52;
}

// 0x41B3A0 (33 B) -- the IsCarrier pair plus the IsReaver pair
BOOL CUnit::IsCarrierType() {
  return this->wType == 0x48 || this->wType == 0x52 ||
         this->wType == 0x53 || this->wType == 0x51;
}

// 0x41B3D0 (23 B) -- 0x53 Reaver, 0x51 Reaver (hero)
BOOL CUnit::IsReaver() {
  return this->wType == 0x53 || this->wType == 0x51;
}

// Turns the geyser on the build site into a new refinery of bUnitType owned by this unit's player.
// TransformType takes a WORD, so its argument and the life index are two extensions of the byte;
// the byte keeps its own web, which takes EBX and is reread from [ebp+8] at both uses.
CUnit *CUnit::ReplaceGeyser(BYTE bUnitType) {
  CUnit *pGeyser = this->pOrderTarget;

  if (pGeyser == NULL || pGeyser->wType != 0xBC)
    pGeyser = sub_4132B0(0xBC, this->wOrderTargetX, this->wOrderTargetY);

  if (pGeyser == NULL)
    return NULL;

  if (is_placing_addon())
    pGeyser->RemovePlaceboxSelf();

  count_remove_completion(pGeyser, 1);
  pGeyser->dwFlags &= ~1u;
  pGeyser->SetPlayer(this->bPlayer, 0);
  pGeyser->SetOwnerColor(this->bPlayer);
  pGeyser->TransformType(bUnitType);
  pGeyser->SetButtonset(0xE7);
  pGeyser->pSprite->AllocImageBelow(0x159, 0, 0, 0);
  pGeyser->lHitPoints = (LONG)((DWORD)HandicapLife(gxUnitLife[bUnitType], pGeyser->bPlayer) / 10);  // Hyperion: at the owner's handicap

  return pGeyser;
}

BOOL CUnit::AddBuildQueue(int nUnitType) {
  unsigned int nCurrent = bBuildQueueSlot;
  unsigned int nFound = 5;
  unsigned int nSlot = nCurrent;
  int nLeft = 5;
  do {
    --nLeft;
    if (nSlot >= 5)
      nSlot = 0;
    if (wBuildQueue[nSlot] == 0xE4) {
      nFound = nSlot;
      break;
    }
    ++nSlot;
  } while (nLeft != 0);
  if (nFound == 5)
    return 0;
  if (!bldg_get_unit_cost(bPlayer, nUnitType, nFound == nCurrent, 1))
    return 0;
  wBuildQueue[nFound] = nUnitType;
  if (!(gulUnitTypeFlags[(WORD)nUnitType] & 1))
    bldg_deduct_resources(bPlayer);
  return 1;
}

BOOL CUnit::AIAddBuildQueue(int nUnitType, int nKind, intptr_t nValue) {
  unsigned int nFound = 5;
  unsigned int nSlot = bBuildQueueSlot;
  int nLeft = 5;
  do {
    --nLeft;
    if (nSlot >= 5)
      nSlot = 0;
    if (wBuildQueue[nSlot] == 0xE4) {
      nFound = nSlot;
      break;
    }
    ++nSlot;
  } while (nLeft != 0);
  if (nFound == 5)
    return 0;
  if (!AddBuildQueue(nUnitType))
    return 0;
  bldg_set_slot(this, nFound, nKind, nValue);
  return 1;
}

// Cancels every queued slot, last first, then clears the queue.
void CUnit::RemoveAllBuildQueue() {
  for (int i = 4; i >= 0; i--) {
    if (wBuildQueue[(bBuildQueueSlot + i) % 5] != 0xE4)
      RemoveBuildQueue(i);
  }
  for (int i = 0; i < 5; i++)
    wBuildQueue[i] = 0xE4;
  bBuildQueueSlot = 0;
}

// Cancels the last occupied queue slot; RemoveBuildQueue is inlined here.
void CUnit::sub_41B6F0() {
  for (int i = 4; i >= 0; i--) {
    if (wBuildQueue[(bBuildQueueSlot + i) % 5] != 0xE4) {
      RemoveBuildQueue(i);
      return;
    }
  }
}

void CUnit::RemoveBuildQueue(int nQueueSlot) {
  if ((unsigned int)nQueueSlot > 4)
    return;
  int nSlot = (bBuildQueueSlot + nQueueSlot) % 5;
  WORD wQueued = wBuildQueue[nSlot];
  if (wQueued == 0xE4)
    return;
  if (nQueueSlot == 0 && dwUnionEC != 0)
    reinterpret_cast<CUnit *>(dwUnionEC)->CancelBuild();
  else if (!(gulUnitTypeFlags[wQueued] & 1))
    bldg_add_resources(bPlayer, wQueued);
  for (int nLeft = 4 - nQueueSlot; nLeft != 0;) {
    --nLeft;
    if (wBuildQueue[nSlot] == 0xE4)
      break;
    int nNext = (nSlot + 1) % 5;
    wBuildQueue[nSlot] = wBuildQueue[nNext];
    bldg_move_slot(this, nSlot, nNext);
    nSlot++;
    nSlot %= 5;
  }
  wBuildQueue[nSlot] = 0xE4;
}

// CUnit::ModifyLife
// The whole body is a tail call: the incoming stack slot is rewritten in place and the jmp
// goes to CUnit::SetLife. MSVC emits it only while SetLife is not inlined, so SetLife stays
// in this TU and large enough not to be spliced in.
void CUnit::ModifyLife(int nDelta) {
  this->SetLife(nDelta + this->lHitPoints);
}

// Repair pricing: the ore and gas spent per step and the step length.
// Each arm rereads the life table, which keeps MSVC from sharing one nLife * 3;
// the gain is read inline, and gas clamps through its own temporary.
void sub_41B910(CUnit *pUnit, int *pnOre, int *pnGas, WORD *pwTime) {
  int nOre = guwUnitMineralsCost[pUnit->wType];
  int nGas = guwUnitGasesCost[pUnit->wType];
  int nBase;
  if (nOre != 0 && nGas != 0)
    nBase = nOre < nGas ? nOre : nGas;
  else
    nBase = nOre > nGas ? nOre : nGas;
  if (nBase == 0) {
    *pnOre = 0;
    *pnGas = 0;
    *pwTime = 0xFFFF;
    return;
  }
  int nLife = HandicapLife(gxUnitLife[pUnit->wType], pUnit->bPlayer);  // Hyperion: at the owner's handicap
  int nCheat = glCheatBits & 2;
  WORD wTime = nLife * 3 / ((WORD)(nCheat ? pUnit->wHPGain << 4 : pUnit->wHPGain) * nBase);
  if (wTime == 0) {
    wTime = 1;
    if (nOre != 0) {
      nOre = (WORD)(nCheat ? pUnit->wHPGain << 4 : pUnit->wHPGain) * nOre /
             (HandicapLife(gxUnitLife[pUnit->wType], pUnit->bPlayer) * 3);
      if (nOre < 1)
        nOre = 1;
    }
    if (nGas != 0) {
      int n = (WORD)(nCheat ? pUnit->wHPGain << 4 : pUnit->wHPGain) * nGas /
              (HandicapLife(gxUnitLife[pUnit->wType], pUnit->bPlayer) * 3);
      if (n < 1)
        n = 1;
      nGas = n;
    }
  } else {
    if (nBase == nOre) {
      nGas = nGas / nOre;
      nOre = 1;
    } else {
      nOre = nOre / nGas;
      nGas = 1;
    }
  }
  *pnOre = nOre;
  *pnGas = nGas;
  *pwTime = wTime;
}

BOOL build_check_money(int nPlayer, int nOre, int nGas) {
  if (nOre > gPlayerMinerals[nPlayer]) {
    gamesnd_show_player_error(nPlayer, byte_5264C2 + 0x93, 0x352);
    return 0;
  }
  if (nGas > gPlayerGas[nPlayer]) {
    gamesnd_show_player_error(nPlayer, byte_5264C2 + 0x96, 0x353);
    return 0;
  }
  return 1;
}

// Charges one repair step. +0xCC is written as a WORD, the step length.
BOOL CUnit::sub_41BAB0(CUnit *pTarget) {
  int nGas;
  int nOre;
  WORD wTime;
  sub_41B910(pTarget, &nOre, &nGas, &wTime);
  if (nOre > gPlayerMinerals[bPlayer]) {
    gamesnd_show_player_error(bPlayer, byte_5264C2 + 0x93, 0x352);
  } else if (nGas > gPlayerGas[bPlayer]) {
    gamesnd_show_player_error(bPlayer, byte_5264C2 + 0x96, 0x353);
  } else {
    *reinterpret_cast<WORD *>(_unmapped_CC) = wTime;
    gPlayerMinerals[bPlayer] -= nOre;
    gPlayerGas[bPlayer] -= nGas;
    return 1;
  }
  return 0;
}

// CUnit::SetLife
// The second `dwFlags & 1` test is a re-read across an opaque call, so two separate if
// statements, not one around both calls. The clamp is signed, so nLife is an int, and the
// store is made before the compare and again on the clamped path, as a plain assignment and
// `if (x > max) x = max;` on the field give. The visibility test is the sprite's visibility
// byte ANDed with gdwLocalMask's low byte, as in src117/lang/usel.cpp.
void CUnit::SetLife(int nLife) {
  this->lHitPoints = nLife;
  // Hyperion: the maximum at the owner's handicap
  const LONG lMax = HandicapLife(gxUnitLife[this->wType], this->bPlayer);
  if (nLife > lMax)
    this->lHitPoints = lMax;

  CSprite *pSprite = this->pSprite;
  if ((pSprite->flags.b & 8) && (pSprite->bVisibilityFlags & (BYTE)gdwLocalMask))
    pSprite->RedrawHP();

  if (this->dwFlags & 1) {
    if (img_dmgOverlay[this->pSprite->pMainImage->wImageID] != 0)
      this->UpdateDmgOverlays();
  }

  if (this->dwFlags & 1)
    tai_assign_unit_strength(this);
}

// CUnit::SetRallyTarget_xy
//
// The null store is scheduled between the coordinate pair and the source cannot ask for
// that -- it is MSVC filling the load-use gap on the second `mov ax`. The parameter width
// is not proven by this body (an int parameter emits the same truncating store); it is
// taken from the field, which DisplayRallyTarget sign-extends.
void CUnit::SetRallyTarget_xy(spos_t x, spos_t y) {
  this->wRallyX = x;
  this->pRallyUnit = NULL;
  this->wRallyY = y;
}

// CUnit::SetRallyTarget_CUnit
// A null argument rallies the unit to itself. The default is written in place,
// `if (pTarget == NULL) pTarget = this;`, which gives 1.17's cmovz; a ternary inverts it.
// pSprite is reloaded for each coordinate, so the source names `pTarget->pSprite` twice.
void CUnit::SetRallyTarget_CUnit(CUnit *pTarget) {
  if (pTarget == NULL)
    pTarget = this;

  this->pRallyUnit = pTarget;
  this->wRallyX = pTarget->pSprite->wPosX;
  this->wRallyY = pTarget->pSprite->wPosY;
}

CUnit *CUnit::sub_41BC30(int nUnitType, int nArg1) {
  if (!(dwFlags & 2) && wType != 0x48 && wType != 0x52 && wType != 0x53 &&
      wType != 0x51 && wType != 0x24)
    return NULL;
  if (bldg_check_food(bPlayer, nUnitType, nArg1)) {
    upos_t wY = pSprite->wPosY;
    upos_t wX = pSprite->wPosX;
    CUnit *pUnit = CreateUnitAt((WORD)nUnitType, PosS(wX), PosS(wY), bPlayer);
    if (pUnit != NULL)
      return pUnit;
    ShowCreateError(bPlayer);
  }
  return NULL;
}

// Places the queued building at the order target.
// The owner byte goes through its own local; without it the build type is
// never spilled and the frame disappears.
BOOL CUnit::sub_41BCB0() {
  int nBuildType = wBuildQueue[bBuildQueueSlot];
  if (!bldg_get_unit_cost(bPlayer, nBuildType, 1, 1))
    return 0;
  WORD wH = (WORD)gUnitPlaceboxSize[(WORD)nBuildType].y;
  WORD wW = (WORD)gUnitPlaceboxSize[(WORD)nBuildType].x;
  BYTE bOwner = bPlayer;
  if (mtx_query_place_bldg(this, bOwner,
                           (wOrderTargetX - (short)wW / 2) / 32,
                           (wOrderTargetY - (short)wH / 2) / 32,
                           nBuildType, 0, 0, 1, 0)) {
    gamesnd_build_error(this, 0x358);
    return 0;
  }
  bldg_deduct_resources(bPlayer);
  return 1;
}

void CUnit::RallyToFactoryTarget(CUnit *pFactory) {
  if (pFactory != NULL && pFactory->IsRallyer()) {
    CUnit *pRally = pFactory->pRallyUnit;
    if (pRally != pFactory && pFactory->wRallyX != 0) {
      if (pRally != NULL && bPlayer == pRally->bPlayer)
        SetOrder_CUnit(0x31, pRally);
      else
        SetOrder_xy(6, pFactory->wRallyX, pFactory->wRallyY);
    }
  }
}

/// How many of this unit's queued builds are nUnitType. A building answers 0 or 1
// (busy, or the current slot not being that type in a build order); a few
// single-slot producers compare the current slot; everything else counts all five.
int CUnit::CountInBuildQueue(int nUnitType) {
  WORD wType = this->wType;
  if (gulUnitTypeFlags[wType] & 8) {
    DWORD_PTR dwBusy = 0;
    order_id_t bOrder = bOrderId;
    if (bOrder == 0x1E || bOrder == 0x22 || bOrder == 0x21 || IsBuildingProtoss()) {
      dwBusy = (DWORD_PTR)pOrderTarget;
    } else {
      order_id_t bBg = bBackgroundOrderId;
      if (bBg == 0x26 || bBg == 0x3F || IsConstructingAddon())
        dwBusy = dwUnionEC;
    }
    if (dwBusy)
      return 0;
    int nOrder = bOrderId;
    if (wBuildQueue[bBuildQueueSlot % 5] != nUnitType)
      return 0;
    if (nOrder == 0x1E || nOrder == 0x1F || nOrder == 0x46 || nOrder == 0x1A || nOrder == 0x19)
      return 1;
    return 0;
  }
  if (wType == 0x24 || wType == 0x3B || wType == 0x61 || bOrderId == 0x2B || bOrderId == 0x2D)
    return nUnitType == wBuildQueue[bBuildQueueSlot];
  int nCount = 0;
  int nSlot = bBuildQueueSlot;
  for (int i = 0; i < 5; i++) {
    if (wBuildQueue[(nSlot + i) % 5] == nUnitType)
      nCount++;
  }
  return nCount;
}

void CUnit::sub_41BF70(int nArg) {
  reinterpret_cast<CTarget *>(this)->BitSwap();
  IsRallyer();
}

// 0x41BF90 (23 B) -- 0x49 Interceptor, 0x55 Scarab
BOOL CUnit::IsFighterType() {
  return this->wType == 0x49 || this->wType == 0x55;
}

// 0x41BFB0 (43 B) -- 0x83 Hatchery, 0x85 Hive, 0x84 Lair
BOOL CUnit::IsZergTownhall() {
  return this->wType == 0x83 || this->wType == 0x85 || this->wType == 0x84;
}
