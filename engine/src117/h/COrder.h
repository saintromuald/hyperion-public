// COrder.h -- one node in a unit's order queue, and the allocator entry points
// that make and unmake one.
//
// CUnit holds the queue as a head/tail pair at +0x74/+0x78, and every body in
// src117/lang/CUnitOrder.cpp that adds or removes an order goes through the CLink
// primitives on these nodes.
//
// The header owns the COrder.h basename on purpose. A generated TU resolves a
// class to a header by that basename over an include path that also reaches the
// quarantined reference tree, which has a COrder.h of its own; owning the name
// keeps that file out of our build whatever it contains. src117/h/CUnitMisc.h and
// src117/h/CTarget.h exist for the same reason.
#pragma once

#include "platform.h"       // ours: BYTE/WORD/DWORD + <stddef.h> for offsetof
#include "order_target.h"   // ours: the (position, unit) pair stored at +0x0C
#include "typelimits.h"     // Hyperion: order_id_t

// Pointers only, so no definition is needed: an include here would be collision
// surface in the generated TUs, which span both trees.
class CUnit;

// COrder -- one queued order. 0x14 bytes: InitOrders memsets 2,000 x 0x14 =
// 0x9C40 over the pool and strides it with `add esi, 14h`, and no body reads past
// the DWORD at +0x10.
class COrder {
public:
  /*0x00*/ COrder        *pPrev;   // inferred from the CLink node contract; no body reads it
  /*0x04*/ COrder        *pNext;
  /*0x08*/ order_id_t     bOrderId;  // Hyperion: 16 bits on x64
  /*0x09*/ BYTE           _unmapped_09[0x01];
  /*0x0A*/ WORD           wUnitType;
  /*0x0C*/ order_target_t target;

  // 0x413C80. Static and __fastcall: ECX carries the order id, and only its low
  // byte (`mov cl, bl`), EDX the unit type, and the two stack dwords are one
  // order_target_t by value. The id has to be a BYTE, since an int makes the
  // compiler materialise all of ECX; spelled as a separate point and unit, the
  // +0x0A WORD store sinks four bytes past the oracle.
  static COrder * __fastcall AllocOrder(order_id_t bOrderId, int nUnitType,
                                        order_target_t target);

  // 0x413D50. __thiscall, nullary and void: CUnit::RemovePreviousOrder reaches it
  // as a tail jump, and that caller's own callers ignore EAX.
  void FreeOrder();

  // 0x413D70 and 0x413D90, the static id-to-pointer pair. An id is 1-based.
  static COrder *GetOrder(int nId);
  static int GetOrderID(COrder *pOrder);

  // 0x413DB0. Zeroes the pool and threads every node onto the free list. Static
  // and nullary; both callers discard EAX.
  static
  void InitOrders();

  // 0x413CD0 and 0x414010. The save pass walks the whole pool backwards, turning
  // every COrder * into a 1-based pool index and every target unit into its id;
  // UnfixupSave is the inverse. Both are static and nullary.
  static void FixupSave();
  static void UnfixupSave();

  // 0x413ED0. True once 1800 of the 2000 nodes are in use. Nullary.
  static BOOL NearLimit();

  // 0x413EE0 and 0x413E10. ECX is the save file and not a `this`: it is forwarded
  // untouched to compress, decompress, pfwrite and pfread, and under /Gr a static
  // member takes its first argument in ECX.
  static BOOL SaveOrders(void *pFile);
  static BOOL LoadOrders(void *pFile);

  // Declared for the call only, and neither can ship: both reach 0x4BE950, the
  // one-byte `ret` that /OPT:ICF folds every empty function onto, so a body
  // calling them links against nothing. SaveOrders calls one before the compress
  // pass and the other after, so the oracle carries two empty members here.
  void PreSave();
  void PostLoad();
};

// The machine check on the layout above; these emit no code.
OC_X86_LAYOUT(sizeof(COrder)              == 0x14, "COrder size      -- InitOrders: 2000 x 14h = 9C40h, the memset size");
static_assert(offsetof(COrder, pPrev)     == 0x00, "COrder::pPrev    [ecx]          -- the CLink node contract (INFERRED)");
OC_X86_LAYOUT(offsetof(COrder, pNext)     == 0x04, "COrder::pNext    [esi+4h] dword -- CUnit::RemoveOrder's queue walk");
OC_X86_LAYOUT(offsetof(COrder, bOrderId)  == 0x08, "COrder::bOrderId [esi+8h] movzx byte -- indexes order_highlight");
OC_X86_LAYOUT(offsetof(COrder, wUnitType) == 0x0A, "COrder::wUnitType[esi+0Ah] word -- AllocOrder's EDX parameter");
OC_X86_LAYOUT(offsetof(COrder, target)    == 0x0C, "COrder::target   [esi+0Ch]/[esi+10h] -- AllocOrder's two stack args");
