// CUnitRClick.cpp -- the right-click order dispatch: it asks the query table
// what order the click produces and installs it. Bodies are in oracle VA order.
//
// A callee defined in this file can be spliced into a caller here by /Ob2, while the
// oracle, built with WPO off, cannot splice across a TU boundary: a callee left in another
// TU reproduces that for free, and __declspec(noinline) is the lever when one must live
// here.

#include "oc_data.h"  // Hyperion: OC_VA
#include "../h/CUnitRClick.h"
#include "../h/CUnit.h"
#include "../h/CThingy.h"      // ours: GetRClickTarget
#include "../h/statbtn.h"      // ours: OrderAllowed
#include "../h/select.h"       // ours: AllowedErr
#include "../h/net_cmds.h"     // ours: get_allowed_err
#include "../gen/globals117.h"

// The globals this TU reaches, dual-spelled: one #ifdef per name, never one over the whole
// block, since the generated set grows over time and a single guard would go false and
// undeclare every name under it.

// The query the right click runs, indexed by CUnit::GetRClickAction's result:
// `mov eax, dword ptr [eax*4+508E18h] / call eax`. Nothing here bounds it;
// rclick_interpret switches on the same value under `cmp eax, 5h / jnbe`, so
// the domain is 0..5 and the extent is 6. Name from
// sc-refs/sheets/data_naming_1.17.csv. Hyperion: 1.17's data has a seventh
// row, the first's query again, so the storage the x64 work gives the table holds
// seven.
typedef int (__fastcall *rclick_query_fn)(CUnit *pUnit, int nX, int nY,
                                          CUnit **ppTarget, WORD wUnitType);
#ifndef OC_query_click
#define OC_query_click
#ifdef OC_DLL_MODE
#define query_click (reinterpret_cast<rclick_query_fn *>(OC_VA(0x508E18u)))
#else
extern rclick_query_fn query_click[7];
#endif
#endif

// Scalar BYTE, written and never read here: `mov byte ptr [0x67CC68], 17h`.
// Name from sc-refs/sheets/data_naming_1.17.csv.
#ifndef OC_byte_67CC68
#define OC_byte_67CC68
#ifdef OC_DLL_MODE
#define byte_67CC68 (*reinterpret_cast<BYTE *>(OC_VA(0x67CC68u)))
#else
extern BYTE byte_67CC68;
#endif
#endif

// The bodies, in oracle VA order.

// 0x431200. One step of a unit's right-click order: run the click query while
// the target square is visible, otherwise just walk there.
void CUnit::DispatchRClick()
{
  switch (bOrderState) {
  case 0:
    if (SetDestination_xy(wOrderTargetX, wOrderTargetY))
      bOrderState = 1;
    break;

  case 1:
    {
      int nX = wOrderTargetX;
      int nY = wOrderTargetY;

      if ((gpSquareMap[(nY / 32) * gwMapWidth + (nX / 32)] &
           (1 << bPlayer)) == 0) {
        // +0x50 is a WORD inside CUnit's _unmapped_50 run.
        sub_47F530(this, nX, nY, *(WORD *)_unmapped_50);
      }
    }
    break;
  }
}

// 0x47F530. Runs the right-click query for one unit and installs whatever order
// it names: a rally point, a targeted order or a move.
void sub_47F530(CUnit *pUnit, int nX, int nY, int nUnitType)
{
  CUnit *pTarget;
  order_id_t bAction;  // Hyperion: a click action, then an order

  if (pUnit == NULL)
    bAction = 0;
  else {
    bAction = pUnit->GetRClickAction();
    if ((pUnit->dwFlags & 0x40000000) && (bAction == 4 || bAction == 5))
      bAction = 1;
  }

  pTarget = NULL;
  byte_67CC68 = 0x17;
  pTarget = GetRClickTarget(nX, nY, nUnitType);
  bAction = (BYTE)query_click[bAction](pUnit, nX, nY, &pTarget, 0xE4);

  if (bAction == 0x17) {
    pUnit->StopUnitMovement();
    pUnit->EndDispatchOrder();
    return;
  }

  if (bAction == 8)
    bAction = gubUnitOrderAttackUnit[pUnit->wType];

  if (OrderAllowed(bAction, pUnit->bPlayer, pUnit) != 1) {
    AllowedErr(get_allowed_err());
    return;
  }

  if (bAction == 0x27) {
    if (pUnit->IsRallyer())
      pUnit->SetRallyTarget_CUnit(pTarget);
    return;
  }

  if (bAction == 0x28) {
    if (pUnit->IsRallyer())
      pUnit->SetRallyTarget_xy(nX, nY);
    return;
  }

  if (pTarget != NULL) {
    pUnit->SetNextOrder_CUnit(bAction, pTarget);
    pUnit->EndDispatchOrder();
    return;
  }

  pUnit->SetNextOrder_xy(bAction, nX, nY);
  pUnit->EndDispatchOrder();
}
