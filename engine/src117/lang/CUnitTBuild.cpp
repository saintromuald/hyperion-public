// CUnitTBuild.cpp -- the terran build order steps: the addon build and the
// nuke silo's train step. Bodies are in oracle VA order.
//
// A callee defined in this file can be spliced into a caller here by /Ob2, while the
// oracle, built with WPO off, cannot splice across a TU boundary: a callee left in another
// TU reproduces that for free, and __declspec(noinline) is the lever when one must live
// here.

#include "oc_data.h"  // Hyperion: OC_VA
#include "../h/CUnitTBuild.h"
#include "../h/CUnit.h"
#include "../h/gamesnd.h"        // ours: gamesnd_build_complete
#include "../h/CUnitTSpells.h"   // ours: status_set_update
#include "../h/CSprite.h"
#include "../h/CTarget.h"
#include "../h/point16.h"
#include "../h/SAI_Banker.h"     // ours: bldg_get_unit_cost, bldg_deduct_resources
#include "../h/collide.h"        // ours: mtx_query_place_bldg
#include "../h/CUnitInit.h"      // ours: CreateUnitAt
#include "../h/placebox.h"       // ours: town_move_morphed_building
#include "../gen/globals117.h"

// The globals this TU reaches, dual-spelled: one #ifdef per name, never one over the whole
// block, since the generated set grows over time and a single guard would go false and
// undeclare every name under it.

// glCheatBits @0x5432FC is registered and comes from the generated header;
// this TU only reads it narrowed, `test byte ptr [0x5432FC], 2h`.

// The order a unit type falls back to when its build step ends. BYTE element
// and unbiased: `movzx eax, word ptr [esi+64h]` then `movzx eax, byte ptr
// [eax+520D50h]`. Extent 228 is units.dat's row count.
#ifndef OC_gubUnitOrderAI
#define OC_gubUnitOrderAI
#ifdef OC_DLL_MODE
#define gubUnitOrderAI (reinterpret_cast<BYTE *>(OC_VA(0x520D50u)))
#else
extern order_id_t gubUnitOrderAI[228] OC_UNALIGNED;
#endif
#endif

// 0x543300 gpImageUnit, registered but not yet emitted by gen.
#ifdef OC_DLL_MODE
  #ifndef gpImageUnit
  #define gpImageUnit (*reinterpret_cast<CUnit **>(OC_VA(0x543300u)))
  #endif
#else
extern CUnit *gpImageUnit;
#endif

// Registered units.dat columns, dual-spelled here because gen emits a global
// only once a row using it is marked. gUnitAddOnOffset covers types 106 and
// up, so it is indexed [type - 106] and the oracle folds that to 0x522910.
#ifdef OC_DLL_MODE
  #ifndef gulUnitTypeFlags
  #define gulUnitTypeFlags (reinterpret_cast<DWORD *>(OC_VA(0x521660u)))
  #endif
  #ifndef gubUnitOrderGuard
  #define gubUnitOrderGuard (reinterpret_cast<BYTE *>(OC_VA(0x520F20u)))
  #endif
  #ifndef gUnitPlaceboxSize
  #define gUnitPlaceboxSize (reinterpret_cast<point16_t *>(OC_VA(0x522728u)))
  #endif
  #ifndef gUnitAddOnOffset
  #define gUnitAddOnOffset (reinterpret_cast<point16_t *>(OC_VA(0x522AB8u)))
  #endif
#else
extern DWORD     gulUnitTypeFlags[228] OC_UNALIGNED;
extern order_id_t gubUnitOrderGuard[228] OC_UNALIGNED;
extern point16_t gUnitPlaceboxSize[228] OC_UNALIGNED;
extern point16_t gUnitAddOnOffset[96] OC_UNALIGNED;
#endif

// The bodies, in oracle VA order.

// 0x4355C0. Cancel the addon this building is putting up, if it has not
// finished. DispatchBuildAddon inlines it, so it belongs in this TU and keeps
// __forceinline: MSVC's late inliner leaves the inlined body's tests unfolded,
// as the oracle has them, and without it DispatchBuildAddon is 15 B short.
OC_EMIT __forceinline void CUnit::CancelAddon()
{
  CUnit *pAddon = (CUnit *)dwUnionEC;

  if (pAddon != NULL && (pAddon->dwFlags & 1) == 0)
    pAddon->CancelBuild();
}

// 0x4355E0. One step of a terran building's addon build: advance the addon's
// hit points, then hand the building back its idle order.
void CUnit::DispatchBuildAddon()
{
  DWORD dwUnitFlags = dwFlags;
  CUnit *pAddon = (CUnit *)dwUnionEC;

  if (dwUnitFlags & 0x400)
    return;
  if (bLockdownTimer != 0)
    return;
  if (bStasisTimer != 0)
    return;
  if (bParalyzeTimer != 0)
    return;

  if (pAddon != NULL) {
    if (dwUnitFlags & 2) {
      WORD wGain = pAddon->wHPGain;
      if (glCheatBits & 2)
        wGain <<= 4;
      pAddon->Build(wGain, 0);
      if ((pAddon->dwFlags & 1) == 0)
        return;
      gamesnd_build_complete(this);
      sub_4359C0(pAddon);
    } else if ((pAddon->dwFlags & 1) == 0) {
      CancelAddon();
    }
  }

  // +0xC4 is a WORD store into CUnit's _unmapped_C4 run, not the dword union
  // arm declared over it.
  *(WORD *)_unmapped_C4 = 0xE4;
  if (dwUnion134 != 0)
    SetOrder(gubUnitOrderAI[wType]);
  SetBackgroundOrder(0x17);
}

// 0x4356C0. The nuclear silo's train step: latch the nuke being built, then
// finish when it completes.
void CUnit::DispatchNukeTrain()
{
  switch (bOrderState) {
  case 0:
    {
      CUnit *pNuke = (CUnit *)dwUnionEC;
      if (pNuke != NULL) {
        pUnionD0 = pNuke;
        pNuke->pUnion80 = this;
        bOrderState = 1;
      }
    }
    break;

  case 1:
    {
      CUnit *pNuke = pUnionD0;
      if (pNuke == NULL) {
        bOrderState = 0;
        break;
      }
      if (pNuke->dwFlags & 1) {
        pUnionD4 = (void *)1;
        status_set_update();
        bOrderState = 0;
        if (dwUnion134 != 0)
          SetOrder(0x9C);
      }
    }
    break;
  }
}

// 0x435730. Order handler for placing an addon: land at the spot if lifted,
// then test the addon's footprint, pay for it and start it. size is read before
// wSelfType, and CreateUnitAt takes the addon type through a local.
void CUnit::DispatchPlaceAddon()
{
  switch (bOrderState) {
  case 0:
    if (!(gulUnitTypeFlags[*(WORD *)_unmapped_C4] & 2))
      return;
    if (!(dwFlags & 2)) {
      SetOrder_xy(0x47, wOrderTargetX, wOrderTargetY);
      SetOrder_xy_interrupt(0x24, wOrderTargetX, wOrderTargetY, 0);
      return;
    }
    {
      int nY = wOrderTargetY;
      int nX = wOrderTargetX;
      if (!reinterpret_cast<CTarget *>(this)->XYInRadius(0, nX, nY)) {
        SetOrder_xy(0x48, wOrderTargetX, wOrderTargetY);
        SetOrder_xy_interrupt(0x24, wOrderTargetX, wOrderTargetY, 0);
        return;
      }
    }
    if (!SetDestination_xy(wOrderTargetX, wOrderTargetY))
      return;
    bOrderState = 1;
  case 1: {
    int nState;
    if (wMoveTargetX == pSprite->wPosX && wMoveTargetY == PosU(pSprite->wPosY))
      nState = (dwFlags & 0x80000) ? 2 : 1;
    else
      nState = 0;
    if (!nState)
      return;
    WORD wAddon = *(WORD *)_unmapped_C4;
    point16_t size = gUnitPlaceboxSize[wAddon];
    WORD wSelfType = wType;
    point16_t offset = gUnitAddOnOffset[wAddon - 106];
    wOrderTargetX -= gUnitPlaceboxSize[wSelfType].x / 2;
    wOrderTargetY -= gUnitPlaceboxSize[wSelfType].y / 2;
    int nSx = size.x;
    wOrderTargetX += nSx / 2 + offset.x;
    wOrderTargetY += size.y / 2 + offset.y;
    if (!bldg_get_unit_cost(bPlayer, wAddon, 1, 1)) {
      EndDispatchOrder();
      return;
    }
    if (mtx_query_place_bldg(this, bPlayer,
                             // A volatile read of the spilled nSx evaluates the size term first.
                             wOrderTargetX / 32 - *(volatile int *)&nSx / 32 / 2,
                             wOrderTargetY / 32 - size.y / 32 / 2,
                             *(WORD *)_unmapped_C4, 0, 0, 1, 0)) {
      gamesnd_build_error(this, 0x358);
      EndDispatchOrder();
      return;
    }
    WORD wNew = *(WORD *)_unmapped_C4;
    CUnit *pNew = CreateUnitAt(wNew, wOrderTargetX, wOrderTargetY, bPlayer);
    if (pNew == NULL) {
      ShowCreateError(bPlayer);
      return;
    }
    bldg_deduct_resources(bPlayer);
    SetOrder(gubUnitOrderGuard[wType]);
    SetBackgroundOrder(0x25);
    dwUnionEC = (DWORD_PTR)pNew;
    pNew->SetOrder(0x2C);
    town_move_morphed_building(this, pNew);
    pNew->pUnion80 = this;
    status_set_update();
    break;
  }
  }
}

// 0x435990. TRUE while this building is still putting up its addon.
BOOL CUnit::IsConstructingAddon()
{
  if (bBackgroundOrderId == 0x25 && (dwFlags & 2)) {
    CUnit *pAddon = (CUnit *)dwUnionEC;
    if (pAddon && !(pAddon->dwFlags & 1))
      return TRUE;
  }
  return FALSE;
}

// 0x4359C0. Attach a finished addon: record it, hand it this building's
// owner, and play its attach animation with the addon as the iscript unit.
void CUnit::sub_4359C0(CUnit *pUnit)
{
  pAddon = pUnit;
  pUnit->SetPlayer(bPlayer, 0);
  pUnit->SetOwnerColor(bPlayer);

  CUnit *pSaved = gpImageUnit;
  gpImageUnit = pUnit;
  pUnit->pSprite->SetAnimation(0x11, 1);
  gpImageUnit = pSaved;

  if (wType == 0x6C && pUnionD0 != NULL)
    pUnionD0->SetPlayer(bPlayer, 0);
}
