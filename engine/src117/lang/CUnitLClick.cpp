// CUnitLClick.cpp -- the left-click order dispatch: it turns a queued left
// click into an issued order. Bodies are in oracle VA order.
//
// A callee defined in this file can be spliced into a caller here by /Ob2, while the
// oracle, built with WPO off, cannot splice across a TU boundary: a callee left in another
// TU reproduces that for free, and __declspec(noinline) is the lever when one must live
// here.

#include "oc_data.h"  // Hyperion: OC_VA
#include "../h/CUnitLClick.h"
#include "../h/CUnit.h"
#include "../h/CThingy.h"      // ours: GetRClickTarget
#include "../gen/globals117.h"

// The globals this TU reaches, dual-spelled: one #ifdef per name, never one over the whole
// block, since the generated set grows over time and a single guard would go false and
// undeclare every name under it.

// The order a left click issues, indexed by the unit's current order id. BYTE
// element and unbiased: `movzx ecx, byte ptr [ebx+4Dh]` then `mov cl, byte ptr
// [ecx+51BC80h]`. The extent is left incomplete because nothing bounds it: the
// sheet has no next name and no initialiser reaches it.
#ifndef OC_gubOrderThingyRemap
#define OC_gubOrderThingyRemap
#ifdef OC_DLL_MODE
#define gubOrderThingyRemap (reinterpret_cast<BYTE *>(OC_VA(0x51BC80u)))
#else
extern order_id_t gubOrderThingyRemap[] OC_UNALIGNED;
#endif
#endif

// The bodies, in oracle VA order.

// 0x427C90. One step of a unit's left-click order: while the target square is
// visible, issue the click's order to the unit and to its turret.
void CUnit::DispatchLClick()
{
  switch (bOrderState) {
  case 0:
    // A unit without 0x20000 advances without a destination, which is the
    // oracle's shape: the flag test branches into the state store, not past it.
    if ((dwFlags & 0x20000) == 0 ||
        SetDestination_xy(wOrderTargetX, wOrderTargetY))
      bOrderState = 1;
    break;

  case 1:
    {
      int nX = wOrderTargetX;
      int nY = wOrderTargetY;
      CUnit *pTarget;
      order_id_t bOrder;

      if (gpSquareMap[(nY / 32) * gwMapWidth + (nX / 32)] & (1 << bPlayer))
        break;

      // +0x50 is a WORD inside CUnit's _unmapped_50 run.
      pTarget = GetRClickTarget(nX, nY, *(WORD *)_unmapped_50);
      if (pTarget != NULL) {
        bOrder = gubOrderThingyRemap[bOrderId];
        if (bOrder == 8) {
          CUnit *pSub = subunit;
          bOrder = gubUnitOrderAttackUnit[wType];
          if (IsTurret(pSub)) {
            pSub->SetNextOrder_CUnit(gubUnitOrderAttackUnit[pSub->wType],
                                     pTarget);
            pSub->EndDispatchOrder();
          }
        }
        SetNextOrder_CUnit(bOrder, pTarget);
      }
      EndDispatchOrder();
    }
    break;
  }
}
