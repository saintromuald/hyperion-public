// CUnitOrder.cpp -- the unit order queue: installing orders, removing them, and ending a
// dispatch. Bodies are in oracle VA order.
//
// Nearly every body builds an order_target_t -- an 8-byte { point16_t pt; CUnit *pUnit } --
// and passes it by value, the pUnit half pushed first; the `sub esp, 8h` frame all over
// this TU is that struct (src117/h/order_target.h).
//
// bQueuedOrderCount (+0x84) counts only orders with an order_highlight entry, and the
// highlight test guards the decrement alone: the underflow clamp is a separate statement,
// and nesting it makes the jz skip the whole block where 1.17 skips only the `dec`.
//
// The heavy cores this file calls -- the installers and ManageQueuedOrders -- are in other
// TUs on purpose: 1.17 is WPO-off, so its calls to them cannot be inlined, and defining
// one here would let /Ob2 splice it in.
#include "oc_data.h"  // Hyperion: OC_VA
#include "../h/platform.h"
#include "../h/order_target.h"   // ours: the 8-byte (position, unit) pair -- see above
#include "../h/COrder.h"         // ours: the queue node, and AllocOrder/FreeOrder
#include "../h/CLink.h"          // ours: Unlink/PrependLink/AppendLink/PrependThis
#include "../h/CSprite.h"        // ours: wPosX/wPosY -- PrependOrder_CUnit reads the
                                 // target's sprite for its point half
#include "../h/CUnit.h"
#include "../h/order.h"          // ours: status_get_str + map_msg_target_self,
                                 // the AllocOrder-failed error pair
#include "../h/SAI_Main.h"      // ours: SAI_RemoveUnit
#include "../h/statbtn.h"       // ours: TechUseAllowed
#include "../h/CTarget.h"       // ours: CTarget::SetTargetOrder_*
#include "../h/CUnitOrder.h"    // ours: unit_race_matches_refinery_bldg
#include "../h/hxrules.h"       // Hyperion: HxRules
#include "../gen/globals117.h"
#include "../h/handicap.h"  // Hyperion: a player's handicap

// order_highlight (0x51B980) -- a WORD table indexed by COrder::bOrderId, compared against
// -1 to decide whether an order counts toward bQueuedOrderCount. Dual-spelled: a plain
// extern would leave it a relocation in both objects and the shipped form unchecked.
#ifndef order_highlight
#ifdef OC_DLL_MODE
#define order_highlight (reinterpret_cast<short *>(OC_VA(0x51B980u)))
#else
extern short order_highlight[] OC_UNALIGNED;
#endif
#endif

// CUnit::SetOrder -- oracle 0x42A2C0, 52 B.
// The zero-point installer, and the body that settles how the target local is spelled: it
// builds no coordinates at all, yet the oracle still reserves 8 bytes and stores a zero
// dword to [ebp-8] while pushing a register for both halves. A 4-byte point local reserves
// 4 (`push ecx`) and loses 2 bytes.
void CUnit::SetOrder(order_id_t bOrderId) {
  order_target_t t;
  t.pt.x = 0;
  t.pt.y = 0;
  t.pUnit = NULL;
  bUnion93 |= 1;
  SetOrder_Target_type_interrupt(bOrderId, t, 0xE4, TRUE);
  ManageQueuedOrders();
}

// CUnit::SetOrder_xy -- oracle 0x42A340, 64 B.
// The canonical builder: two int parameters narrowed into the point half. The narrowing
// casts are not decorative -- the parameters are full dwords and the fields 16-bit, so the
// truncating store `mov word ptr [ebp-8h], ax` is what the oracle emits. Twelve more bodies
// in this TU are this shape with different sources for the coordinates.
void CUnit::SetOrder_xy(order_id_t bOrderId, int x, int y) {
  order_target_t t;
  t.pt.x = (short)x;
  t.pt.y = (short)y;
  t.pUnit = NULL;
  bUnion93 |= 1;
  SetOrder_Target_type_interrupt(bOrderId, t, 0xE4, TRUE);
  ManageQueuedOrders();
}

// CUnit::RemoveQueuedOrder -- oracle 0x429D20, 71 B.
// Unlink one node, clamp the counter, free it. The counter shape is the TU-wide one in the
// file header: the highlight test guards only the decrement.
//
// The CLink cast is free and it is the tree's convention: CLink.h does not make its users
// derive from CLink, so callers cast at the call site, and with the links at offset 0 the
// cast is an identity. The oracle hands Unlink the (head, tail) pair as `lea eax,[ecx+74h]`
// and `lea eax,[ecx+78h]` with the COrder in ECX, which is what types both CUnit fields.
void CUnit::RemoveQueuedOrder(COrder *pOrder) {
  if (order_highlight[pOrder->bOrderId] != -1)
    --bQueuedOrderCount;
  if (bQueuedOrderCount == 0xFF)
    bQueuedOrderCount = 0;
  reinterpret_cast<CLink *>(pOrder)->Unlink(
      reinterpret_cast<CLink **>(&pOrderQueueHead),
      reinterpret_cast<CLink **>(&pOrderQueueTail));
  pOrder->FreeOrder();
}

// The zero-point, zero-target forwarders: 0x429EB0, 0x4295A0 and 0x429DE0. All three are
// the same 35-byte shape and differ only in the installer they call. The oracle folds the
// whole struct to one register, and the lone `mov dword ptr [ebp-8h], eax` is the point
// being zeroed as one dword (both `t.pt.x = 0` and `t.pt.y = 0` coalesce); the pUnit half
// at [ebp-4h] is never written, because its only use is the push, which MSVC satisfies
// from the known-zero register. Reserving 8 and writing 4 is what pins
// sizeof(order_target_t) == 8.

void CUnit::SetOrder_interrupt(int nOrderId, int nFlag) {
  order_target_t t;
  t.pt.x = 0;
  t.pt.y = 0;
  t.pUnit = NULL;
  SetOrder_Target_type_interrupt(nOrderId, t, 0xE4, nFlag);
}

void CUnit::PrependOrder(int nOrderId, COrder *pPrevOrder) {
  order_target_t t;
  t.pt.x = 0;
  t.pt.y = 0;
  t.pUnit = NULL;
  PrependOrder_Target_type(nOrderId, t, 0xE4, pPrevOrder);
}

// 0x4295D0 takes the same (int, order_target_t, int, COrder *) shape as its Prepend twin,
// `ret 14h` reading [ebp+18h].
void CUnit::AppendOrder(int nOrderId, COrder *pPrevOrder) {
  order_target_t t;
  t.pt.x = 0;
  t.pt.y = 0;
  t.pUnit = NULL;
  AppendOrder_Target_type(nOrderId, t, 0xE4, pPrevOrder);
}

// The xy forwarders with no queue kick: 0x429EE0 and 0x429D70. The canonical point builder
// minus the `or [esi+93h], 1` and the trailing ManageQueuedOrders, with a forwarded fourth
// argument in place of the constant TRUE. The point half is reloaded from the frame here,
// where it was computed, while the pUnit half is still folded to an immediate because it
// is still a constant; the asymmetry with the zero-point group is the compiler's.

void CUnit::SetOrder_xy_interrupt(int nOrderId, int x, int y, BOOL bInterrupt) {
  order_target_t t;
  t.pt.x = (short)x;
  t.pt.y = (short)y;
  t.pUnit = NULL;
  SetOrder_Target_type_interrupt(nOrderId, t, 0xE4, bInterrupt);
}

void CUnit::PrependOrder_xy(int nOrderId, int x, int y, COrder *pPrevOrder) {
  order_target_t t;
  t.pt.x = (short)x;
  t.pt.y = (short)y;
  t.pUnit = NULL;
  PrependOrder_Target_type(nOrderId, t, 0xE4, pPrevOrder);
}

// The pure forwarders, no frame at all: 0x42A0C0 and 0x42A3E0. These two are the clearest
// confirmation that (pt, pUnit) is one by-value struct: they own an order_target_t as a
// parameter and hand it straight on, and the oracle forwards the two slots as a contiguous
// pair with no local at all -- no `sub esp`, just `push [ebp+10h] / push [ebp+0Ch]`.

void CUnit::SetOrder_Target_interrupt(int nOrderId, order_target_t t, BOOL bInterrupt) {
  SetOrder_Target_type_interrupt(nOrderId, t, 0xE4, bInterrupt);
}

// 0x42A3E0 -- the same forward, but it also claims the queue: `or byte ptr
// [esi+93h], 1h` is scheduled between the pushes and the call, and
// ManageQueuedOrders follows. Source order is set-flag / install / manage; the
// scheduling is MSVC's.
void CUnit::SetOrder_Target(order_id_t bOrderId, order_target_t t) {
  bUnion93 |= 1;
  SetOrder_Target_type_interrupt(bOrderId, t, 0xE4, TRUE);
  ManageQueuedOrders();
}

// The type-carrying xy installer, 0x42A300: the canonical shape with one substitution, and
// that substitution is what separates the unit-type slot from the interrupt flag across
// the family. The third installer argument is this body's own [ebp+0Ch] where every other
// forwarder pushes the constant 0xE4, and the fourth is still the constant 1.
void CUnit::SetOrder_type_xy(order_id_t bOrderId, int nUnitType, int x, int y) {
  order_target_t t;
  t.pt.x = (short)x;
  t.pt.y = (short)y;
  t.pUnit = NULL;
  bUnion93 |= 1;
  SetOrder_Target_type_interrupt(bOrderId, t, nUnitType, TRUE);
  ManageQueuedOrders();
}

// The target-derived point, 0x429DA0. This is the body that types order_target_t::pUnit as
// a CUnit *: the same value is stored into the struct's high half and dereferenced as a
// unit in the one function. The single `mov edx, [esi+0Ch]` feeding both sprite loads is
// the common subexpression of one `pTarget->pSprite`; writing the member chain twice is
// what produces it.
//
// The third parameter is a COrder *, and the evidence is the callee's: this body forwards
// the slot untouched into PrependOrder_Target_type, which loads it into ECX as `this` for
// CLink::PrependLink, so it is a list node whatever a given caller keeps there. The call
// sites do not settle it -- the slot is fed from CUnit +0x74 at some and +0x7C at others.
void CUnit::PrependOrder_CUnit(int nOrderId, CUnit *pTarget, COrder *pPrevOrder) {
  order_target_t t;
  t.pt.x = pTarget->pSprite->wPosX;
  t.pt.y = pTarget->pSprite->wPosY;
  t.pUnit = pTarget;
  PrependOrder_Target_type(nOrderId, t, 0xE4, pPrevOrder);
}

// The globals, dual-spelled: a plain extern stays a symbol in both compiles and would
// leave the shipped form's baked slots unchecked.

// gubUnitOrderGuard -- BYTE[228] @0x520F20, indexed by CUnit::wType; the value is used
// directly as an order id.
#ifndef gubUnitOrderGuard
#ifdef OC_DLL_MODE
#define gubUnitOrderGuard (reinterpret_cast<BYTE *>(OC_VA(0x520F20u)))
#else
extern order_id_t gubUnitOrderGuard[228] OC_UNALIGNED;
#endif
#endif

// stru_665B3C -- an 8-byte order_target_t @0x665B3C. The extent comes from the sheet, which
// names this address and then nothing until sgbUpdateLos_g at 0x665B44. The type comes from
// CUnit::sub_42A850 and sub_42A880, which end their argument build with
// `push dword ptr [0x665B40] / push dword ptr [0x665B3C]` -- an 8-byte object pushed high
// dword first into sub_42A8B0's last parameter slot. Nothing in the corpus writes it, so
// it stays zero for the life of the process and those two forwarders always pass a null
// target.
#ifndef stru_665B3C
#ifdef OC_DLL_MODE
#define stru_665B3C (*reinterpret_cast<order_target_t *>(OC_VA(0x665B3Cu)))
#else
extern order_target_t stru_665B3C;
#endif
#endif

// CUnit::GetFirstQueuedOrder -- oracle 0x429D10, 7 B.
order_id_t CUnit::GetFirstQueuedOrder() {
  return pOrderQueueHead->bOrderId;
}

// CUnit::EndUninterruptableDispatchOrder -- oracle 0x429D00, 15 B.
void CUnit::EndUninterruptableDispatchOrder() {
  dwFlags &= ~0x1000;
  EndDispatchOrder();
}


// CUnit::EndNoBreakCodeSection -- oracle 0x429CC0, 50 B.
void CUnit::EndNoBreakCodeSection() {
  dwFlags &= ~0x2000;
  pSprite->flags.b &= 0x7F;
  if (pOrderQueueHead != NULL && (bUnion93 & 1)) {
    BreakAnim();
    ManageQueuedOrders();
  }
}

// CUnit::RemovePreviousOrder -- oracle 0x42A140, 63 B.
void CUnit::RemovePreviousOrder() {
  COrder *pOrder = pOrderQueueHead;
  if (order_highlight[pOrder->bOrderId] != -1)
    --bQueuedOrderCount;
  if (bQueuedOrderCount == 0xFF)
    bQueuedOrderCount = 0;
  reinterpret_cast<CLink *>(pOrder)->Unlink(
      reinterpret_cast<CLink **>(&pOrderQueueHead),
      reinterpret_cast<CLink **>(&pOrderQueueTail));
  pOrder->FreeOrder();
}

// Sets the background order and clears its target and state. The pragma is the only shape
// that gives the oracle's `xor eax,eax` register stores; /Ox emits three immediates.
#pragma optimize("s", on)
void CUnit::ReplaceBackgroundOrder(order_id_t bOrderId) {
  bBackgroundOrderId = bOrderId;
  dwUnionE8 = 0;
  dwUnionEC = 0;
  bUnionE2 = 0;
}
#pragma optimize("", on)

// CUnit::RemoveAllOrders -- oracle 0x429AC0, 84 B.
void CUnit::RemoveAllOrders() {
  while (pOrderQueueHead != NULL) {
    COrder *pOrder = pOrderQueueHead;
    if (order_highlight[pOrder->bOrderId] != -1)
      --bQueuedOrderCount;
    if (bQueuedOrderCount == 0xFF)
      bQueuedOrderCount = 0;
    reinterpret_cast<CLink *>(pOrder)->Unlink(
        reinterpret_cast<CLink **>(&pOrderQueueHead),
        reinterpret_cast<CLink **>(&pOrderQueueTail));
    pOrder->FreeOrder();
  }
}

// CUnit::RemoveOrder -- oracle 0x42A0E0, 93 B.
void CUnit::RemoveOrder(order_id_t bOrderId) {
  COrder *pOrder = pOrderQueueHead;
  while (pOrder != NULL) {
    if (pOrder->bOrderId == bOrderId) {
      if (order_highlight[bOrderId] != -1)
        --bQueuedOrderCount;
      if (bQueuedOrderCount == 0xFF)
        bQueuedOrderCount = 0;
      reinterpret_cast<CLink *>(pOrder)->Unlink(
          reinterpret_cast<CLink **>(&pOrderQueueHead),
          reinterpret_cast<CLink **>(&pOrderQueueTail));
      pOrder->FreeOrder();
      return;
    }
    pOrder = pOrder->pNext;
  }
}

// CUnit::PrependOrder_Target_type -- oracle 0x429E10, 118 B.
//
// The BYTE first parameter is part of the code. The oracle loads the order id as a BYTE and
// then schedules the next push before the register copy (`mov bl, byte ptr [ebp+8h]` /
// `push dword ptr [ebp+0Ch]` / `mov cl, bl`); with an int parameter MSVC loads the full
// dword and the last two swap. A BYTE local does not substitute for it, because MSVC
// materialises the whole dword slot for an int even when only BL is read. It is
// caller-neutral: a caller forwarding its own int parameter emits the same push either
// way, which is what every Prepend and SetNextOrder caller does.
void CUnit::PrependOrder_Target_type(order_id_t bOrderId, order_target_t t, int nUnitType,
                                     COrder *pPrevOrder) {
  if (dwUnion134 != 0 && bQueuedOrderCount > 8)
    return;

  COrder *pOrder = COrder::AllocOrder(bOrderId, nUnitType, t);
  if (pOrder == NULL) {
    map_msg_target_self(status_get_str(0x368), bPlayer);
    return;
  }

  if (order_highlight[bOrderId] != -1)
    ++bQueuedOrderCount;

  reinterpret_cast<CLink *>(pPrevOrder)->PrependLink(
      reinterpret_cast<CLink *>(pOrder),
      reinterpret_cast<CLink **>(&pOrderQueueHead));
}

// -- callees not ours ------------------------------------------------------

// 0x4297F0: `mov ecx, edi / call IsTypeNeutralResource` where EDI holds the unit type. The
// parameter is 16-bit, and what proves it is what is not there: EDI was filled by a dword
// read of a WORD parameter slot whose upper half is undefined, and MSVC moves it into ECX
// with no widening. An int parameter would have forced `movzx ecx, di`. The result is
// tested with `test eax, eax`, so BOOL.
BOOL __fastcall IsTypeNeutralResource(WORD wUnitType);

// CUnit::sub_42A850 -- 0x42A850, 43 B. A pure forwarder.
void CUnit::sub_42A850(int nEnable, order_id_t nOrderId, int nUnitType, int x, int y) {
  sub_42A8B0(2, nEnable, nOrderId, NULL, nUnitType, x, y, stru_665B3C);
}

// CUnit::sub_42A880 -- 0x42A880, 44 B. The same forwarder with a target unit instead of
// coordinates: the null goes in the (x, y) slots and the type slot takes the family's 0xE4
// "no unit type" sentinel.
void CUnit::sub_42A880(int nEnable, order_id_t nOrderId, CUnit *pTarget) {
  sub_42A8B0(1, nEnable, nOrderId, pTarget, 0xE4, 0, 0, stru_665B3C);
}

// CUnit::Suicide -- 0x42A4D0, 89 B.
void CUnit::Suicide() {
  DropInventory();
  RemoveAllOrders();
  order_target_t t;
  t.pt.x = wOrderTargetX;
  t.pt.y = wOrderTargetY;
  t.pUnit = NULL;
  bUnion93 |= 1;
  SetOrder_Target_type_interrupt(0, t, 0xE4, TRUE);
  ManageQueuedOrders();
  SAI_RemoveUnit(this, 1);
}

// CUnit::EndDispatchOrder_xy -- 0x42A530, 105 B.
//
// The queue-non-empty arm calls ManageQueuedOrders and returns, as EndDispatchOrder below
// does, but without setting bUnion93. With nothing between the branch and the call, that
// arm's call and epilogue are identical to the function's own tail, so MSVC tail-merges the
// two and lands the branch on the `call` itself rather than on the `mov ecx, esi` two bytes
// above it, ECX still holding `this` on that path. Written as a bare return, the byte at
// 0x0E reads 4Ch or 53h instead of 4Eh: the same instruction count, the wrong merge point.
// The asymmetry with EndDispatchOrder is 1.17's -- there the `or byte ptr [esi+93h], 1h`
// sits in the way and the two epilogues stay separate.
void CUnit::EndDispatchOrder_xy(int x, int y) {
  if (pOrderQueueHead != NULL) {
    ManageQueuedOrders();
    return;
  }
  if (dwUnion134 != 0) {
    order_target_t t;
    t.pt.x = 0;
    t.pt.y = 0;
    t.pUnit = NULL;
    bUnion93 |= 1;
    SetOrder_Target_type_interrupt(0x9C, t, 0xE4, TRUE);
  } else {
    order_id_t bOrder = gubUnitOrderGuard[wType];
    order_target_t t;
    t.pt.x = (short)x;
    t.pt.y = (short)y;
    t.pUnit = NULL;
    bUnion93 |= 1;
    SetOrder_Target_type_interrupt(bOrder, t, 0xE4, TRUE);
  }
  ManageQueuedOrders();
}

// CUnit::EndDispatchOrder -- 0x42A5A0, 109 B.
void CUnit::EndDispatchOrder() {
  if (pOrderQueueHead != NULL) {
    bUnion93 |= 1;
    ManageQueuedOrders();
    return;
  }
  if (dwUnion134 != 0) {
    order_target_t t;
    t.pt.x = 0;
    t.pt.y = 0;
    t.pUnit = NULL;
    bUnion93 |= 1;
    SetOrder_Target_type_interrupt(0x9C, t, 0xE4, TRUE);
  } else {
    order_id_t bOrder = gubUnitOrderGuard[wType];
    order_target_t t;
    t.pt.x = 0;
    t.pt.y = 0;
    t.pUnit = NULL;
    bUnion93 |= 1;
    SetOrder_Target_type_interrupt(bOrder, t, 0xE4, TRUE);
  }
  ManageQueuedOrders();
}

// CUnit::ErrorLClick -- 0x4297F0, 128 B.
//
// 1.17's behaviour is reproduced as found: when bAction is neither 0x23 nor 0x50 the body
// falls straight into the writer with ESI never assigned, so `*pwError` receives whatever
// the caller had in ESI. wErr is left uninitialised on purpose, to reproduce that.
BOOL CUnit::ErrorLClick(order_id_t bAction, WORD wUnitType, int x, int y,
                        WORD *pwError) {
  WORD wErr;

  switch (bAction) {
  case 0x50:
    wErr = (WORD)(GetRace() + 0x37D);
    if (wUnitType != 0xBC) {
      if (IsTypeNeutralResource(wUnitType))
        return TRUE;
      wErr = 0x37C;
      if (unit_race_matches_refinery_bldg(this, wUnitType))
        return TRUE;
    }
    break;
  case 0x23:
    wErr = 0x374;
    if (wUnitType >= 0x6A && wUnitType <= 0x7F)
      return TRUE;
    break;
  default:
    return TRUE;
  }

  if (pwError != NULL)
    *pwError = wErr;
  return FALSE;
}

// AppendOrder_Target_type with no predecessor, for the three bodies the oracle expands it
// into. A call to the real one would inline into SetNextOrder and friends too, which the
// oracle does not do.
static __forceinline void append_order_at_tail(CUnit *pUnit, order_id_t bOrderId,
                                               order_target_t t, int nUnitType) {
  COrder *pOrder = COrder::AllocOrder(bOrderId, nUnitType, t);
  if (pOrder == NULL) {
    map_msg_target_self(status_get_str(0x368), pUnit->bPlayer);
    return;
  }
  if (order_highlight[bOrderId] != -1)
    ++pUnit->bQueuedOrderCount;
  CLink **ppTail = reinterpret_cast<CLink **>(&pUnit->pOrderQueueTail);
  if (*ppTail != NULL)
    (*ppTail)->AppendLink(reinterpret_cast<CLink *>(pOrder), ppTail);
  else
    reinterpret_cast<CLink *>(pOrder)->PrependThis(
        reinterpret_cast<CLink **>(&pUnit->pOrderQueueHead), ppTail);
}

// Nonzero when the unit is inside an uninterruptable or no-break section.
// The two bits are tested separately; one `& 0x3000` compiles branchless.
BOOL CUnit::IsUninterruptable() {
  if ((dwFlags & 0x1000) || (dwFlags & 0x2000))
    return TRUE;
  return FALSE;
}

// The inverse of IsUninterruptable.
BOOL CUnit::IsInterruptable() {
  if ((dwFlags & 0x1000) || (dwFlags & 0x2000))
    return FALSE;
  return TRUE;
}

void CUnit::PrependOrder_Target(int nOrderId, order_target_t t, COrder *pPrevOrder) {
  PrependOrder_Target_type(nOrderId, t, 0xE4, pPrevOrder);
}

// gpImageUnit @0x543300, the sheet's name; spelled as in CUnitCombat.cpp.
#ifndef gpImageUnit
#ifdef OC_DLL_MODE
#define gpImageUnit (*reinterpret_cast<CUnit **>(OC_VA(0x543300u)))
#else
extern CUnit *gpImageUnit;
#endif
#endif

// Plays an animation on the subunit's sprite with gpImageUnit pointing at it.
void CUnit::SetAnimTurret(int nAnim) {
  gpImageUnit = subunit;
  subunit->pSprite->SetAnimation(nAnim, 1);
  gpImageUnit = this;
}

// Sets the background order when it changes, clearing its target and state. The pragma is
// the only shape that gives the oracle's `xor eax,eax` register stores; /Ox emits three
// immediates.
#pragma optimize("s", on)
void CUnit::SetBackgroundOrder(order_id_t bOrderId) {
  if (bBackgroundOrderId != bOrderId) {
    bBackgroundOrderId = bOrderId;
    dwUnionE8 = 0;
    dwUnionEC = 0;
    bUnionE2 = 0;
  }
}
#pragma optimize("", on)

void CUnit::AppendOrder_xy(int nOrderId, int x, int y, COrder *pPrevOrder) {
  order_target_t t;
  t.pt.x = (short)x;
  t.pt.y = (short)y;
  t.pUnit = NULL;
  AppendOrder_Target_type(nOrderId, t, 0xE4, pPrevOrder);
}

void CUnit::AppendOrder_CUnit(order_id_t bOrderId, CUnit *pTarget, COrder *pPrevOrder) {
  order_target_t t;
  if (pTarget != NULL) {
    t.pt.x = pTarget->pSprite->wPosX;
    t.pt.y = pTarget->pSprite->wPosY;
  } else {
    t.pt.x = 0;
    t.pt.y = 0;
  }
  t.pUnit = pTarget;
  AppendOrder_Target_type(bOrderId, t, 0xE4, pPrevOrder);
}

void CUnit::SetOrder_CUnit_interrupt(int nOrderId, CUnit *pTarget, BOOL bInterrupt) {
  order_target_t t;
  if (pTarget != NULL) {
    t.pt.x = pTarget->pSprite->wPosX;
    t.pt.y = pTarget->pSprite->wPosY;
  } else {
    t.pt.x = 0;
    t.pt.y = 0;
  }
  t.pUnit = pTarget;
  SetOrder_Target_type_interrupt(nOrderId, t, 0xE4, bInterrupt);
}

void CUnit::SetOrder_CUnit(order_id_t bOrderId, CUnit *pTarget) {
  order_target_t t;
  bUnion93 |= 1;
  if (pTarget != NULL) {
    t.pt.x = pTarget->pSprite->wPosX;
    t.pt.y = pTarget->pSprite->wPosY;
  } else {
    t.pt.x = 0;
    t.pt.y = 0;
  }
  t.pUnit = pTarget;
  SetOrder_Target_type_interrupt(bOrderId, t, 0xE4, TRUE);
  ManageQueuedOrders();
}

// Queues an order behind the current one, or at the end of an empty queue.
void CUnit::SetNextOrder(int nOrderId) {
  if (pOrderQueueHead != NULL)
    PrependOrder(nOrderId, pOrderQueueHead);
  else
    AppendOrder(nOrderId, NULL);
}

void CUnit::SetNextOrder_xy(order_id_t bOrderId, int nX, int nY) {
  if (pOrderQueueHead != NULL)
    PrependOrder_xy(bOrderId, nX, nY, pOrderQueueHead);
  else
    AppendOrder_xy(bOrderId, nX, nY, NULL);
}

void CUnit::SetNextOrder_Target(int nOrderId, order_target_t t) {
  if (pOrderQueueHead != NULL)
    PrependOrder_Target(nOrderId, t, pOrderQueueHead);
  else
    AppendOrder_Target_type(nOrderId, t, 0xE4, NULL);
}

void CUnit::SetNextOrder_CUnit(order_id_t bOrderId, CUnit *pTarget) {
  if (pOrderQueueHead != NULL) {
    PrependOrder_CUnit(bOrderId, pTarget, pOrderQueueHead);
  } else {
    AppendOrder_CUnit(bOrderId, pTarget, NULL);
  }
}

// Whether a unit type is the refinery building of the unit's own race.
BOOL __fastcall unit_race_matches_refinery_bldg(CUnit *pUnit, WORD wUnitType) {
  BYTE bRace = pUnit->GetRace();
  if ((bRace == 0 && wUnitType == 0x95) || (bRace == 1 && wUnitType == 0x6E) ||
      (bRace == 2 && wUnitType == 0x9D))
    return TRUE;
  return FALSE;
}

// Installs an order, the payload chosen by nKind.
void CUnit::SetOrderEx(int nKind, order_id_t bOrderId, CUnit *pTarget, int nUnitType,
                       int x, int y, order_target_t t) {
  switch (nKind) {
  case 0:
    SetOrder(bOrderId);
    break;
  case 1:
    SetOrder_CUnit(bOrderId, pTarget);
    break;
  case 2:
    SetOrder_type_xy(bOrderId, nUnitType, x, y);
    break;
  case 3:
    SetOrder_xy(bOrderId, x, y);
    break;
  case 4:
    SetOrder_Target(bOrderId, t);
    break;
  }
}

// order_canBeQueued (0x51B380), BYTE per order id; the sheet's name.
#ifndef order_canBeQueued
#ifdef OC_DLL_MODE
#define order_canBeQueued (reinterpret_cast<BYTE *>(OC_VA(0x51B380u)))
#else
extern BYTE order_canBeQueued[];
#endif
#endif

// Queues the order when asked to and both the order and the unit's current
// order allow it, otherwise replaces the current order.
void CUnit::sub_42A8B0(int nKind, int nEnable, order_id_t nOrderId, CUnit *pTarget,
                       int nUnitType, int x, int y, order_target_t t) {
  if (nEnable != 0 && order_canBeQueued[nOrderId] != 0) {
    switch (bOrderId) {
    case 0x02:
    case 0x03:
    case 0x17:
    case 0x5D:
    case 0x98:
    case 0xAF:
      break;
    default:
      if (bQueuedOrderCount >= 8) {
        map_msg_target_self(status_get_str(0x367), bPlayer);
        return;
      }
      if (COrder::NearLimit()) {
        map_msg_target_self(status_get_str(0x369), bPlayer);
        return;
      }
      AppendOrderEx(nKind, nOrderId, pTarget, nUnitType, x, y, t);
      return;
    }
  }
  SetOrderEx(nKind, nOrderId, pTarget, nUnitType, x, y, t);
}

// Puts an order at the front of the queue.
void CUnit::SetForegroundOrder(order_id_t bOrderId) {
  if (pOrderQueueHead != NULL) {
    PrependOrder(bOrderId, pOrderQueueHead);
    return;
  }
  order_target_t t;
  t.pt.x = 0;
  t.pt.y = 0;
  t.pUnit = NULL;
  append_order_at_tail(this, bOrderId, t, 0xE4);
}

// Validates a right-click order against its target; on failure writes the
// error string id through pwError and returns FALSE.
BOOL CUnit::ErrorRClick(order_id_t bErrorKind, CUnit *pTarget, int nX, int nY,
                        WORD *pwError) {
  WORD wErr;

  switch (bErrorKind) {
  case 0xB1:
    if (pTarget == NULL)
      return TRUE;
    if (!TechUseAllowed(0x22, bPlayer, this))
      return FALSE;
    wErr = 0x52D;
    if (!(pTarget->dwFlags & 6) &&
        (gulUnitTypeFlags[pTarget->wType] & 0x10000) &&
        pTarget->lHitPoints < HandicapLife(gxUnitLife[pTarget->wType], pTarget->bPlayer))
      return TRUE;
    break;
  case 0x22:
    wErr = 0x374;
    if (pTarget == NULL || pTarget->GetRace() != 1)
      break;
    wErr = 0x373;
    if ((gulUnitTypeFlags[pTarget->wType] & 0x40000000) &&
        (pTarget->dwFlags & 1) &&
        pTarget->lHitPoints < HandicapLife(gxUnitLife[pTarget->wType], pTarget->bPlayer))
      return TRUE;
    break;
  case 0x4F:
    if (pTarget == NULL)
      return TRUE;
    wErr = (WORD)(GetRace() + 0x37D);
    if (pTarget->wType == 0xBC)
      break;
    if (pTarget->IsNeutralResource())
      return TRUE;
    wErr = 0x37C;
    if (!(gulUnitTypeFlags[pTarget->wType] & 0x2000))
      break;
    wErr = 0x380;
    if (bPlayer != pTarget->bPlayer)
      break;
    return TRUE;
  case 0x5E:
    wErr = 0x37A;
    if (pTarget == NULL || bPlayer != pTarget->bPlayer)
      break;
    if (pTarget->CanEnterTransport(this))
      return TRUE;
    if (pTarget->CanFitInTransport(this))
      wErr = 0x37B;
    break;
  default:
    return TRUE;
  }

  if (pwError != NULL)
    *pwError = wErr;
  return FALSE;
}

// order_canBeInterrupted (0x51B200), BYTE per order id; spelled as in
// CUnitInit.cpp.
#ifdef OC_DLL_MODE
  #ifndef order_canBeInterrupted
  #define order_canBeInterrupted (reinterpret_cast<BYTE *>(OC_VA(0x51B200u)))
  #endif
#else
extern BYTE order_canBeInterrupted[189] OC_UNALIGNED;
#endif

// Installs an order: trims the queue's tail of orders it replaces, then for
// order 0x6D pushes it in front of the current one, otherwise appends it.
void CUnit::SetOrder_Target_type_interrupt(order_id_t bNewOrder, order_target_t t,
                                           int nUnitType, BOOL bInterrupt) {
  if (bOrderId == 0)
    return;

  COrder *pTail;
  while ((pTail = pOrderQueueTail) != NULL &&
         ((bInterrupt && order_canBeInterrupted[pTail->bOrderId]) ||
          pTail->bOrderId == bNewOrder))
    RemoveQueuedOrder(pOrderQueueTail);

  // Hyperion: by Hyperion's rules, StarCraft: Remastered's limit on the orders the game
  // gives (HX_OPT_ORDER_QUEUE): one is dropped when the unit's queue holds 8 or the order
  // pool is at 90%, as a player's queued command is (sub_42A8B0), unless the unit is undoing
  // a worker's transparency, which re-issues itself while a harvest is queued. An SCV putting
  // up a building keeps the order that brings it down from the air (DispatchConstructBuilding)
  // whatever its queue holds, or it stays a flyer.
  if (HxRules() && (g_hxRuleOptions & HX_OPT_ORDER_QUEUE) != 0) {
    if (bOrderId == 0x21 && bNewOrder == 0x96 && bQueuedOrderCount >= 8)
      append_order_at_tail(this, bNewOrder, t, nUnitType);
    if ((COrder::NearLimit() || bQueuedOrderCount >= 8) && bOrderId != 0x97)
      return;
  }

  if (bNewOrder == 0x6D) {
    order_target_t tCur = OrderTarget();
    if (pOrderQueueHead != NULL)
      PrependOrder_Target_type(bOrderId, tCur, *(WORD *)_unmapped_50, pOrderQueueHead);
    else
      AppendOrder_Target_type(bOrderId, tCur, *(WORD *)_unmapped_50, NULL);
    PrependOrder_Target(0x6D, t, pOrderQueueHead);
    EndUninterruptableDispatchOrder();
    return;
  }

  append_order_at_tail(this, bNewOrder, t, nUnitType);
}

// gubUnitOrderAttackTile -- BYTE[228] @0x5210F0, spelled as in select.cpp.
#ifdef OC_DLL_MODE
  #ifndef gubUnitOrderAttackTile
  #define gubUnitOrderAttackTile (reinterpret_cast<BYTE *>(OC_VA(0x5210F0u)))
  #endif
#else
extern order_id_t gubUnitOrderAttackTile[228] OC_UNALIGNED;
#endif

// ord_unk4 -- BYTE per order id @0x51B080, the sheet's name. A nonzero entry
// lets a turret subunit take its parent's order unchanged.
#ifndef ord_unk4
#ifdef OC_DLL_MODE
#define ord_unk4 (reinterpret_cast<BYTE *>(OC_VA(0x51B080u)))
#else
extern BYTE ord_unk4[] OC_UNALIGNED;
#endif
#endif

// True for a unit whose type is a turret (a subunit). CUnit::IsTurret has the
// same test out of line in CUnitCombat.cpp; the oracle inlines it here.
static __forceinline bool is_turret_unit(CUnit *pUnit) {
  return pUnit && (gulUnitTypeFlags[pUnit->wType] & 0x10);
}

// Starts the next queued order: dequeues it, makes it current, and hands the
// matching order to a turret subunit.
void CUnit::ManageQueuedOrders() {
  COrder *pOrder = pOrderQueueHead;
  if (dwUnion134 != 0 && pOrder != NULL && (dwFlags & 0x10) &&
      pOrder->bOrderId != 0 && pOrder->bOrderId != 0x76 &&
      pOrder->bOrderId != 0x75 && pOrder->bOrderId != 0x9C &&
      wType != 0x0D &&
      !(wType == 0x67 && (pOrder->bOrderId == 2 || pOrder->bOrderId == 0x0B))) {
    PrependOrder_xy(0x76, PosS(pSprite->wPosX), pSprite->wPosY, pOrder);
    EndUninterruptableDispatchOrder();
    return;
  }

  if ((dwFlags & 0x3000) && (pOrder == NULL || pOrder->bOrderId != 0))
    return;
  if (pOrder == NULL)
    return;

  RemoveQueuedOrder(pOrder);
  // The oracle reads the node after COrder::FreeOrder has released it.
  // Two copies of the order byte give the oracle's two stack homes.
  order_id_t bSubOrder = pOrder->bOrderId;
  order_id_t bOrder = bSubOrder;
  int wOrderType = pOrder->wUnitType;
  order_target_t t = pOrder->target;

  dwFlags = (dwFlags & 0xF7FFEFF7) |
            (((order_canBeInterrupted[bOrder] == 0) & 1) << 12);
  bUnion85 = 0;
  bUnion93 &= ~1;
  bUnionE3 = 0;
  CTarget *pTarget = reinterpret_cast<CTarget *>(this);
  if (t.pUnit != NULL)
    pTarget->SetTargetOrder_CUnit(bSubOrder, t.pUnit);
  else if ((WORD)wOrderType != (WORD)0xE4)
    pTarget->SetTargetOrder_fogged_xy(bSubOrder, wOrderType, t.pt.x, t.pt.y);
  else
    pTarget->SetTargetOrder_xy(bSubOrder, t.pt.x, t.pt.y);
  if (dwUnion134 == 0)
    dwUnion7C = 0;
  BreakAnim();

  if (!(gulUnitTypeFlags[wType] & 0x10) && is_turret_unit(subunit)) {
    if (bOrder == gubUnitOrderGuard[wType])
      bSubOrder = gubUnitOrderGuard[subunit->wType];
    else if (bOrder == gubUnitOrderAttackUnit[wType])
      bSubOrder = gubUnitOrderAttackUnit[subunit->wType];
    else if (bOrder == gubUnitOrderAttackTile[wType])
      bSubOrder = gubUnitOrderAttackTile[subunit->wType];
    else if (!ord_unk4[bOrder])
      return;
    if (t.pUnit != NULL)
      subunit->SetOrder_Target(bSubOrder, t);
    else
      subunit->SetOrder_type_xy(bSubOrder, wOrderType, t.pt.x, t.pt.y);
  }
}

// Appends an order after pPrevOrder, or at the tail of the queue when it is NULL.
// noinline keeps the SetNextOrder family calling it, as the oracle does.
__declspec(noinline) void CUnit::AppendOrder_Target_type(order_id_t bOrderId, order_target_t t,
                                                         int nUnitType, COrder *pPrevOrder) {
  COrder *pOrder = COrder::AllocOrder(bOrderId, nUnitType, t);
  if (pOrder == NULL) {
    map_msg_target_self(status_get_str(0x368), bPlayer);
    return;
  }
  if (order_highlight[bOrderId] != -1)
    ++bQueuedOrderCount;
  CLink **ppTail = reinterpret_cast<CLink **>(&pOrderQueueTail);
  if (pPrevOrder != NULL)
    reinterpret_cast<CLink *>(pPrevOrder)->AppendLink(reinterpret_cast<CLink *>(pOrder), ppTail);
  else if (*ppTail != NULL)
    (*ppTail)->AppendLink(reinterpret_cast<CLink *>(pOrder), ppTail);
  else
    reinterpret_cast<CLink *>(pOrder)->PrependThis(
        reinterpret_cast<CLink **>(&pOrderQueueHead), ppTail);
}

// Appends an order, the payload chosen by nKind.
void CUnit::AppendOrderEx(int nKind, order_id_t bOrderId, CUnit *pTarget, int nUnitType,
                          int x, int y, order_target_t t) {
  switch (nKind) {
  case 0: {
    order_target_t tNew;
    tNew.pt.x = 0;
    tNew.pt.y = 0;
    tNew.pUnit = NULL;
    append_order_at_tail(this, bOrderId, tNew, 0xE4);
    break;
  }
  case 1:
    AppendOrder_CUnit(bOrderId, pTarget, NULL);
    break;
  case 2: {
    order_target_t tNew;
    tNew.pt.x = (short)x;
    tNew.pt.y = (short)y;
    tNew.pUnit = NULL;
    append_order_at_tail(this, bOrderId, tNew, nUnitType);
    break;
  }
  case 3: {
    order_target_t tNew;
    tNew.pt.x = (short)x;
    tNew.pt.y = (short)y;
    tNew.pUnit = NULL;
    append_order_at_tail(this, bOrderId, tNew, 0xE4);
    break;
  }
  case 4:
    AppendOrder_Target_type(bOrderId, t, 0xE4, NULL);
    break;
  }
}
