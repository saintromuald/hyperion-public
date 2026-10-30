// CUnitPBuild.cpp -- the protoss build order steps: warping a building in and
// the pylon's own build order. Bodies are in oracle VA order.
//
// A callee defined in this file can be spliced into a caller here by /Ob2, while the
// oracle, built with WPO off, cannot splice across a TU boundary: a callee left in another
// TU reproduces that for free, and __declspec(noinline) is the lever when one must live
// here.

#include "oc_data.h"  // Hyperion: OC_VA
#include "../h/CUnitPBuild.h"
#include "../h/CUnit.h"
#include "../h/CSprite.h"
#include "../h/CImage.h"
#include "../h/CTarget.h"        // ours: XYInRadius
#include "../h/TrigAct.h"        // ours: CreateUnitAt
#include "../h/placebox.h"       // ours: town_move_morphed_building
#include "../h/CUnitTSpells.h"   // ours: gamesnd_play
#include "../gen/globals117.h"
#include "../h/handicap.h"  // Hyperion: a player's handicap

// The globals this TU reaches, dual-spelled: one #ifdef per name, never one over the whole
// block, since the generated set grows over time and a single guard would go false and
// undeclare every name under it.

// The warp-flash image for a sprite id. WORD element and unbiased: `movzx eax,
// word ptr [ecx+8h]` (CSprite::wSpriteID) then `movzx eax, word ptr
// [eax*2+51EAA8h]`. Extent 517 is sprites.dat's row count.
#ifndef OC_guwSpriteImage
#define OC_guwSpriteImage
#ifdef OC_DLL_MODE
#define guwSpriteImage (reinterpret_cast<WORD *>(OC_VA(0x51EAA8u)))
#else
extern WORD guwSpriteImage[517];
#endif
#endif

// glCheatBits @0x5432FC and guwUnitEnergy @0x520540 are registered, so they
// come from the generated header. This body reads the second as the per-type
// shield ceiling: `movzx ecx, word ptr [ecx*2+520540h]` against lShields.

// The bodies, in oracle VA order.

// 0x42EE00. The probe's build order: walk to the site, then warp the building
// in and hand it over.
void CUnit::DispatchBuildProtoss()
{
  if (dwUnion134 != 0)
    dwFlags |= 0x800000;

  switch (bOrderState) {
  case 0:
    if (SetDestination_xy(wOrderTargetX, wOrderTargetY))
      bOrderState = 1;
    break;

  case 1:
    {
      int nRadius = wBuildQueue[bBuildQueueSlot] == 0x9D ? 0x58 : 0x46;
      int nY = wOrderTargetY;
      int nX = wOrderTargetX;
      if (!reinterpret_cast<CTarget *>(this)->XYInRadius(nRadius, nX, nY)) {
        if (AtMoveTarget())
          SetDestination_xy(wOrderTargetX, wOrderTargetY);
        break;
      }
      StopUnitMovement();
      bOrderState = 2;
    }
    break;

  case 2:
    if (bMoveFlags & 2)
      break;
    {
      int nRadius = wBuildQueue[bBuildQueueSlot] == 0x9D ? 0x58 : 0x46;
      int nY = wOrderTargetY;
      int nX = wOrderTargetX;
      if (!reinterpret_cast<CTarget *>(this)->XYInRadius(nRadius, nX, nY)) {
        if (AtMoveTarget())
          SetDestination_xy(wOrderTargetX, wOrderTargetY);
        break;
      }
    }
    if (!sub_41BCB0()) {
      EndDispatchOrder();
      break;
    }
    {
      WORD wBuildType = wBuildQueue[bBuildQueueSlot];
      CUnit *pNew;

      if (wBuildType == 0x9D)
        pNew = ReplaceGeyser(0x9D);
      else
        pNew = CreateUnitAt(wBuildType, wOrderTargetX, wOrderTargetY, bPlayer);

      wBuildQueue[bBuildQueueSlot] = 0xE4;
      if (pNew == NULL) {
        ShowCreateError(bPlayer);
        EndDispatchOrder();
        break;
      }

      pNew->pSprite->ChangeImage(0xD3, 0);
      if (wBuildType == 0x9D)
        pNew->pSprite->AllocImageBelow(0x158, 0, 0, 0);
      pOrderTarget = pNew;
      town_move_morphed_building(this, pNew);
      pNew->SetOrder(0x30);
      bOrderState = 3;
      gamesnd_play(0x210, this, 1, 0);
    }
    break;

  case 3:
    if (pOrderTarget != NULL && pOrderTarget->wType == 0x9D &&
        pOrderQueueHead == NULL)
      SetOrder_CUnit(0x52, pOrderTarget);
    else
      EndDispatchOrder();
    pSprite->SetAnimation(0x0C, 1);
    break;
  }
}

// 0x42F020. A warping protoss building's own build order: run the warp-in
// animation, then top its life and shields up each step.
void CUnit::DispatchBuildSelfProtoss()
{
  switch (bOrderState) {
  case 0:
    if (wRemainingBuildTime == 0) {
      pSprite->SetAnimation(0x0D, 1);
      gamesnd_play(0x211, this, 1, 0);
      bOrderState = 1;
    }
    break;

  case 1:
    if (bOrderFlags & 1) {
      bOrderFlags &= ~1;
      pSprite->ChangeImage(guwSpriteImage[pSprite->wSpriteID], 0);
      pSprite->pMainImage->WarpFlashInit(0xD2);
      bOrderState = 2;
    }
    return;

  case 2:
    if (bOrderFlags & 1) {
      bOrderFlags &= ~1;
      pSprite->ChangeImage(guwSpriteImage[pSprite->wSpriteID], 0);
      pSprite->SetAnimation(0x15, 1);
      bOrderState = 3;
    }
    return;

  case 3:
    if ((bOrderFlags & 1) == 0)
      return;
    bOrderFlags &= ~1;
    FinishedMaking();
    UpdateUnitCollision();
    if (dwFlags & 0x400)
      pSprite->SetAnimation(0x18, 1);
    break;
  }

  CountDownToCompletion();

  {
    WORD wLife = wHPGain;
    if (glCheatBits & 2)
      wLife <<= 4;
    ModifyLife(wLife);
  }

  {
    int nOpt = glCheatBits & 2;
    int nShield = wShieldGain;
    WORD wT = wType;
    int nAdd = nShield;

    if (nOpt)
      nAdd <<= 4;

    // The ceiling reads the type through a local; the clamp arm reads the member again.
    // Hyperion: the maximum at the owner's handicap
    if ((int)(HandicapShields(guwUnitEnergy[wT], bPlayer) << 8) < (int)((WORD)nAdd + lShields)) {
      lShields = HandicapShields(guwUnitEnergy[wType], bPlayer) << 8;
      return;
    }

    if (nOpt)
      nShield <<= 4;
    lShields = (WORD)nShield + lShields;
  }
}

// 0x42F1B0. The warp-in animation step for a protoss building placed by a
// probe: swap in the warp flash, then end the order when it finishes.
void CUnit::DispatchProtossWarpIn()
{
  switch (bOrderState) {
  case 0:
    if (bOrderFlags & 1) {
      bOrderFlags &= ~1;
      pSprite->ChangeImage(guwSpriteImage[pSprite->wSpriteID], bUnion21);
      pSprite->SetAnimation(0x15, 1);
      bOrderState = 1;
    }
    break;

  case 1:
    if (bOrderFlags & 1) {
      bOrderFlags &= ~1;
      if (gulUnitTypeFlags[wType] & 0x400000)
        sub_41D200(0x111);
      EndUninterruptableDispatchOrder();
    }
    break;
  }
}

// 0x42F230. The pylon's build order: set the protoss build order and state,
// then run the shared protoss build step.
void CUnit::DispatchPylonBuild()
{
  bOrderId = 0x1F;
  bOrderState = 0x02;
  DispatchBuildProtoss();
}

// 0x42F240. TRUE while a probe is still warping in the building it placed.
BOOL CUnit::IsBuildingProtoss()
{
  if (bOrderId == 0x1F && (dwFlags & 2)) {
    CUnit *pTarget = pOrderTarget;
    if (pTarget && !(pTarget->dwFlags & 1))
      return TRUE;
  }
  return FALSE;
}
