// COrder.cpp -- the order free list: the pool's allocator, its save and load
// passes, and the index-to-pointer fixups a save file needs.
//
// Bodies are in ascending oracle address, 1.17's own order.

#include "oc_data.h"  // Hyperion: OC_VA
#include "../port/data/port_formats.h"  // Hyperion
#include "../gen/save117.h"
#include <string.h>              // memset -- InitOrders zeroes the whole pool
#include "../h/COrder.h"
#include "../h/CLink.h"          // the free list is a CLink head/tail pair
#include "../h/CUnit.h"          // GetUnitID / FromId across the save pass
#include "../h/saveload.h"       // compress / decompress / pfread / pfwrite
#include "../gen/globals117.h"

// The pool extent: 0x7D0 nodes of 0x14 bytes, the 0x9C40 InitOrders memsets.
#define ORDER_POOL_SIZE g_pools.orders

// The globals this TU reaches, each spelled once for the standalone build and
// once for the payload.

// 0x5A52C0 -- the pool itself.
#ifndef OC_SGORDERS
#define OC_SGORDERS
#ifdef OC_DLL_MODE
#define sgOrders (reinterpret_cast<COrder *>(OC_VA(0x5A52C0u)))
#else
extern COrder sgOrders[POOL_ORDERS_STORE] OC_UNALIGNED;
#endif
#endif

// 0x5AEF00 / 0x5AEF04 -- the free list's head and tail, passed together to
// CLink::Unlink and CLink::PrependThis, and spelled as two names the way
// CThingy.cpp spells its four pool pointers.
#ifndef OC_SGPORDERPOOLHEAD_G
#define OC_SGPORDERPOOLHEAD_G
#ifdef OC_DLL_MODE
#define sgpOrderPoolHead_g (*reinterpret_cast<COrder **>(OC_VA(0x5AEF00u)))
#else
extern COrder *sgpOrderPoolHead_g;
#endif
#endif

#ifndef OC_SGPORDERPOOLTAIL_G
#define OC_SGPORDERPOOLTAIL_G
#ifdef OC_DLL_MODE
#define sgpOrderPoolTail_g (*reinterpret_cast<COrder **>(OC_VA(0x5AEF04u)))
#else
extern COrder *sgpOrderPoolTail_g;
#endif
#endif

// 0x5AEF08 -- nodes handed out. AllocOrder increments it, FreeOrder decrements
// it, LoadOrders recomputes it as 2000 minus the free-list length, and NearLimit
// compares it unsigned (`cmp / sbb / inc`).
#ifndef OC_SGNORDERPOOLUSED_G
#define OC_SGNORDERPOOLUSED_G
#ifdef OC_DLL_MODE
#define sgnOrderPoolUsed_g (*reinterpret_cast<DWORD *>(OC_VA(0x5AEF08u)))
#else
extern DWORD sgnOrderPoolUsed_g;
#endif
#endif

// 0x413C80. Pop the free-list head, stamp the order into it, count it used.
COrder *COrder::AllocOrder(order_id_t bOrderId, int nUnitType,
                           order_target_t target)
{
  COrder *pOrder = sgpOrderPoolHead_g;
  if (pOrder == 0)
    return 0;

  pOrder->bOrderId = bOrderId;
  pOrder->wUnitType = (WORD)nUnitType;
  pOrder->target = target;
  reinterpret_cast<CLink *>(pOrder)->Unlink(
      reinterpret_cast<CLink **>(&sgpOrderPoolHead_g),
      reinterpret_cast<CLink **>(&sgpOrderPoolTail_g));
  ++sgnOrderPoolUsed_g;
  return pOrder;
}

// 0x413CD0. Walk the pool backwards replacing every pointer with the index or id
// a save file can hold. The index stays an int: walked as a COrder * the loop
// register picks up a +4 bias and a lea before the exit compare.
void COrder::FixupSave()
{
  int i = ORDER_POOL_SIZE;
  do {
    i--;
    sgOrders[i].target.pUnit =
        reinterpret_cast<CUnit *>((intptr_t)sgOrders[i].target.pUnit->GetUnitID());
    sgOrders[i].pPrev =
        reinterpret_cast<COrder *>((intptr_t)GetOrderID(sgOrders[i].pPrev));
    sgOrders[i].pNext =
        reinterpret_cast<COrder *>((intptr_t)GetOrderID(sgOrders[i].pNext));
  } while (i != 0);
}

// 0x413D50. Push the node back on the free list. `this` is already in ECX, so
// the body is four instructions.
void COrder::FreeOrder()
{
  --sgnOrderPoolUsed_g;
  reinterpret_cast<CLink *>(this)->PrependThis(
      reinterpret_cast<CLink **>(&sgpOrderPoolHead_g),
      reinterpret_cast<CLink **>(&sgpOrderPoolTail_g));
}

// 0x413D70. 1-based index to node.
COrder *COrder::GetOrder(int nId)
{
  if (nId <= 0 || nId > g_pools.orders)  // Hyperion: a save's id past the pool is none
    return 0;
  return &sgOrders[nId - 1];
}

// 0x413D90. The inverse. The difference is a signed ptrdiff, so the division by
// 20 is an `imul 66666667h / sar edx, 3` rather than a shift.
int COrder::GetOrderID(COrder *pOrder)
{
  if (pOrder == 0)
    return 0;
  return (int)(pOrder - sgOrders) + 1;
}

// 0x413DB0. Zero the pool and thread every node onto the free list. The used
// count is assigned from the spent loop counter, so it stores EDI rather than a
// literal 0.
void COrder::InitOrders()
{
  memset(sgOrders, 0, ORDER_POOL_SIZE * sizeof(COrder));
  sgpOrderPoolHead_g = 0;
  sgpOrderPoolTail_g = 0;

  COrder *p = sgOrders;
  int n = ORDER_POOL_SIZE;
  do {
    reinterpret_cast<CLink *>(p)->PrependThis(
        reinterpret_cast<CLink **>(&sgpOrderPoolHead_g),
        reinterpret_cast<CLink **>(&sgpOrderPoolTail_g));
    p++;
  } while (--n != 0);
  sgnOrderPoolUsed_g = n;
}

// 0x413E10. Read the pool back, rebuild the free list, recount what is in use.
BOOL COrder::LoadOrders(void *pFile)
{
  if (!PortSave117Decompress(SAVE117_COrder, sgOrders, ORDER_POOL_SIZE, pFile))  // Hyperion
    return FALSE;

  for (int i = 0; i < ORDER_POOL_SIZE; i++)
    sgOrders[i].PostLoad();
  UnfixupSave();

  if (!PortSaveReadIds(pFile, &sgpOrderPoolHead_g, 1))
    return FALSE;
  if (!PortSaveReadIds(pFile, &sgpOrderPoolTail_g, 1))
    return FALSE;

  sgpOrderPoolHead_g = GetOrder((int)reinterpret_cast<intptr_t>(sgpOrderPoolHead_g));
  sgpOrderPoolTail_g = GetOrder((int)reinterpret_cast<intptr_t>(sgpOrderPoolTail_g));

  int nFree = 0;
  for (COrder *q = sgpOrderPoolHead_g; q != 0; q = q->pNext)
    nFree++;
  sgnOrderPoolUsed_g = ORDER_POOL_SIZE - nFree;
  return TRUE;
}

// 0x413ED0. 1800 of 2000 handed out. The compare is unsigned, hence sbb/inc.
BOOL COrder::NearLimit()
{
  return sgnOrderPoolUsed_g >= g_pools.orders * 9 / 10;
}

// 0x413EE0. Compress the whole pool, then write the free-list head and tail as
// indices. The two pfwrite results collapse into one EDX the epilogue returns.
BOOL COrder::SaveOrders(void *pFile)
{
  FixupSave();
  int i;
  for (i = 0; i < ORDER_POOL_SIZE; i++)
    sgOrders[i].PreSave();

  int nOk = PortSave117Compress(SAVE117_COrder, sgOrders, ORDER_POOL_SIZE, pFile);  // Hyperion

  for (i = 0; i < ORDER_POOL_SIZE; i++)
    sgOrders[i].PostLoad();
  UnfixupSave();

  if (nOk == 0)
    return FALSE;

  sgpOrderPoolHead_g = reinterpret_cast<COrder *>((intptr_t)GetOrderID(sgpOrderPoolHead_g));
  sgpOrderPoolTail_g = reinterpret_cast<COrder *>((intptr_t)GetOrderID(sgpOrderPoolTail_g));

  BOOL bRet;
  if (PortSaveWriteIds(pFile, &sgpOrderPoolHead_g, 1) &&
      PortSaveWriteIds(pFile, &sgpOrderPoolTail_g, 1))
    bRet = TRUE;
  else
    bRet = FALSE;

  sgpOrderPoolHead_g = GetOrder((int)reinterpret_cast<intptr_t>(sgpOrderPoolHead_g));
  sgpOrderPoolTail_g = GetOrder((int)reinterpret_cast<intptr_t>(sgpOrderPoolTail_g));

  return bRet;
}

// 0x414010. FixupSave's inverse: indices and unit ids back to pointers, walked
// with an int index for the same reason.
void COrder::UnfixupSave()
{
  int i = ORDER_POOL_SIZE;
  do {
    i--;
    sgOrders[i].target.pUnit =
        CUnit::FromId((DWORD)reinterpret_cast<DWORD_PTR>(sgOrders[i].target.pUnit));
    sgOrders[i].pPrev = GetOrder((int)reinterpret_cast<intptr_t>(sgOrders[i].pPrev));
    sgOrders[i].pNext = GetOrder((int)reinterpret_cast<intptr_t>(sgOrders[i].pNext));
  } while (i != 0);
}
