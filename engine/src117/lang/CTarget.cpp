// CTarget.cpp -- the order-state base of CUnit and CBullet: what a thing is
// currently ordered at, and everything that installs or clears one.
//
// CTarget is authored flat, as src117/h/CTarget.h explains: the oracle's class
// derives from CFlingy, so every CFlingy member reached here is called through
// a reinterpret_cast of `this`, a no-op because the base sits at offset 0.

#include "../h/CTarget.h"
#include "../h/CFlingy.h"        // ours: Init / BitSwap / PostLoad / SaveEncode
#include "../h/CSprite.h"        // ours: the target's position at +0x14/+0x16
#include "../h/CUnit.h"          // ours: FromId / GetUnitID across the save pass
#include "../gen/globals117.h"

// Hyperion: a bullet is read as a CTarget, as a CFlingy and, through
// gpImageTargeter, as a CUnit, and a unit as a CTarget too, so the fields they share
// fall at the same offsets on either width.
#define OC_SAME_FIELD(a, fa, b, fb) \
  static_assert(offsetof(a, fa) == offsetof(b, fb), #a "::" #fa " is " #b "::" #fb)
OC_SAME_FIELD(CTarget, pSprite, CFlingy, pSprite);
OC_SAME_FIELD(CTarget, _unmapped_14_width, CFlingy, pMoveTargetUnit);
OC_SAME_FIELD(CTarget, lPositionX256, CFlingy, lPositionX256);
OC_SAME_FIELD(CTarget, lPositionY256, CFlingy, lPositionY256);
OC_SAME_FIELD(CTarget, pSprite, CUnit, pSprite);
OC_SAME_FIELD(CTarget, _unmapped_14_width, CUnit, _unmapped_14_width);
OC_SAME_FIELD(CTarget, bPlayer, CUnit, bPlayer);
OC_SAME_FIELD(CTarget, bOrderId, CUnit, bOrderId);
OC_SAME_FIELD(CTarget, bOrderState, CUnit, bOrderState);
OC_SAME_FIELD(CTarget, bOrderFlags, CUnit, bOrderFlags);
OC_SAME_FIELD(CTarget, dwOrderUnion54, CUnit, bUnion54);
OC_SAME_FIELD(CTarget, wOrderTargetX, CUnit, wOrderTargetX);
OC_SAME_FIELD(CTarget, wOrderTargetY, CUnit, wOrderTargetY);
OC_SAME_FIELD(CTarget, pOrderTarget, CUnit, pOrderTarget);
OC_SAME_FIELD(CFlingy, pMoveTargetUnit, CUnit, _unmapped_14_width);
OC_SAME_FIELD(CFlingy, wPositionX, CUnit, wPositionX);
// Hyperion: and the positions, of 4 bytes on x64 (stage E)
OC_SAME_FIELD(CTarget, aFlingyMoveTarget, CFlingy, wMoveTargetX);
OC_SAME_FIELD(CTarget, aFlingyWaypoints, CFlingy, wNextWaypointX);
OC_SAME_FIELD(CTarget, _unmapped_20, CFlingy, bMoveFlags);
OC_SAME_FIELD(CTarget, aFlingyPosition, CFlingy, wPositionX);
OC_SAME_FIELD(CFlingy, wMoveTargetX, CUnit, wMoveTargetX);
OC_SAME_FIELD(CFlingy, wNextWaypointX, CUnit, wNextWaypointX);
OC_SAME_FIELD(CFlingy, wDestinationX, CUnit, wDestinationX);
OC_SAME_FIELD(CFlingy, wPositionY, CUnit, wPositionY);
OC_SAME_FIELD(CFlingy, bMoveFlags, CUnit, bMoveFlags);
OC_SAME_FIELD(CUnit, orderTargetPos.y, CUnit, wOrderTargetY);
OC_SAME_FIELD(CFlingy, lTopSpeed, CUnit, lTopSpeed);
OC_SAME_FIELD(CFlingy, wAcceleration, CUnit, wAcceleration);
#undef OC_SAME_FIELD

// The bodies, in oracle address order, which here is CTarget.h's declaration order and not
// alphabetical: SetTargetOrder_fogged_xy precedes _xy precedes _CUnit.

// 0x4178D0. Forwards to the base, whose own body forwards again and ends at an
// empty function. The tail call is what makes the whole body one jmp.
void CTarget::BitSwap()
{
  reinterpret_cast<CFlingy *>(this)->BitSwap();
}

// 0x4178E0. Build the flingy first; only if that succeeds stamp the order state
// into the CTarget half.
BOOL CTarget::Create(int nFlingyID, spos_t x, spos_t y, int nPlayer,
                     int nDirection)
{
  if (!reinterpret_cast<CFlingy *>(this)->Init((WORD)nFlingyID, x, y, nPlayer,
                                               nDirection))
    return FALSE;

  bPlayer = (BYTE)nPlayer;
  wOrderUnitType = 0xE4;
  bOrderId = 0xBC;
  bOrderState = 0;
  bOrderFlags = 0;
  dwOrderUnion54 = 0;
  pOrderTarget = 0;
  wOrderTargetX = 0;
  wOrderTargetY = 0;
  return TRUE;
}

// 0x417940. The saved id at +0x5C becomes a pointer again. bLinked is not
// forwarded: the base gets a literal 0.
void CTarget::PostLoad(BOOL bLinked)
{
  (void)bLinked;
  reinterpret_cast<CFlingy *>(this)->PostLoad(0);
  pOrderTarget = CUnit::FromId((DWORD)reinterpret_cast<DWORD_PTR>(pOrderTarget));
}

// 0x417960. PostLoad's inverse, and the base call comes last.
void CTarget::PreSave(BOOL bLinked)
{
  (void)bLinked;
  pOrderTarget = reinterpret_cast<CUnit *>((intptr_t)pOrderTarget->GetUnitID());
  reinterpret_cast<CFlingy *>(this)->SaveEncode(0);
}

// 0x417980. Point the order at a unit and cache its sprite position. The oracle
// reloads pSprite for the second coordinate; the store to +0x58 may alias it.
void CTarget::SetFocusTarget(CUnit *pTarget)
{
  pOrderTarget = pTarget;
  if (pTarget != 0) {
    wOrderTargetX = pTarget->pSprite->wPosX;
    wOrderTargetY = pTarget->pSprite->wPosY;
  } else {
    wOrderTargetX = 0;
    wOrderTargetY = 0;
  }
}

// 0x4179B0. An order with no target at all.
void CTarget::SetTargetOrder(order_id_t bOrder)
{
  bOrderId = bOrder;
  pOrderTarget = 0;
  wOrderTargetX = 0;
  wOrderTargetY = 0;
  bOrderState = 0;
  wOrderUnitType = 0xE4;
}

// 0x4179E0. A position plus the unit type remembered under fog, so the order
// survives the target going invisible.
void CTarget::SetTargetOrder_fogged_xy(order_id_t bOrder, int nUnitType, int x, int y)
{
  bOrderId = bOrder;
  wOrderUnitType = (WORD)nUnitType;
  pOrderTarget = 0;
  wOrderTargetX = (short)x;
  wOrderTargetY = (short)y;
  bOrderState = 0;
}

// 0x417A10. A bare position, with the type back to the no-unit sentinel.
void CTarget::SetTargetOrder_xy(order_id_t bOrder, int x, int y)
{
  bOrderId = bOrder;
  wOrderTargetX = (short)x;
  wOrderTargetY = (short)y;
  pOrderTarget = 0;
  bOrderState = 0;
  wOrderUnitType = 0xE4;
}

// 0x417A50. SetFocusTarget plus an order id, and the two trailing stores are
// duplicated into both arms by the compiler rather than by this source.
void CTarget::SetTargetOrder_CUnit(order_id_t bOrder, CUnit *pTarget)
{
  bOrderId = bOrder;
  pOrderTarget = pTarget;
  if (pTarget != 0) {
    wOrderTargetX = pTarget->pSprite->wPosX;
    wOrderTargetY = pTarget->pSprite->wPosY;
  } else {
    wOrderTargetX = 0;
    wOrderTargetY = 0;
  }
  bOrderState = 0;
  wOrderUnitType = 0xE4;
}

// 0x417AA0. Is (x, y) within nRadius of us? The coordinates arrive in pixels and
// are scaled to the x256 fixed point veclen works in; the result is scaled back.
BOOL CTarget::XYInRadius(int nRadius, int x, int y)
{
  DWORD dwDist = (DWORD)veclen(lPositionX256, x << 8, lPositionY256, y << 8);
  return (DWORD)nRadius >= (dwDist >> 8);
}
