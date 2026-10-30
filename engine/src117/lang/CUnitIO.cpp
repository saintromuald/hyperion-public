// CUnitIO.cpp -- unit id hashing and save-pointer fixup. Bodies are in oracle VA order.
//
// A callee defined here can be spliced into a caller here by /Ob2, which the oracle (WPO
// off) never does across a TU; that is what the __declspec(noinline) markers on some
// declarations are for, and why moving a body between TUs can change the code generated.

#include "oc_data.h"  // Hyperion: OC_VA
#include <stdlib.h>                 // _rotl -- GetHash's mixing step

#include "../h/CUnitIO.h"
#include "../h/CUnit.h"
#include "../h/CSprite.h"
#include "../h/CThingy.h"           // GetDoodadThingy / GetDoodadThingyID
#include "../h/CTarget.h"           // PreSave / PostLoad
#include "../h/COrder.h"            // GetOrder / GetOrderID
#include "../h/TAI_GuardPost.h"     // TGuard, guard_id, guard_ptr
#include "../h/SAI_Build.h"         // TWorker, TBldg, worker_*, bldg_*
#include "../h/TAI_Captain.h"       // TSoldier, soldier_id, soldier_ptr
#include "../h/saveload.h"          // get_upgrade_level
#include "../gen/globals117.h"

// The globals this TU reaches, dual-spelled: one #ifdef per name, never one over the
// block, so adding a name cannot undeclare the ones below it.
//
// Before coining a name, look up re/globals117.json by VA key and check
// sc-refs/sheets for an existing one. Minting a name is the master's call.

// dword_69B574 is the base CUnit::pPath is rebased against in both save
// directions: sai_PathPoolBuf, the path pool (src117/lang/sai_path.cpp). Read
// through that name, where the pointer is 8 bytes; 0x69B578 next to it is
// registered as dword_69B578.
#ifdef OC_DLL_MODE
  #ifndef sai_PathPoolBuf
  #define sai_PathPoolBuf (*reinterpret_cast<sai_path **>(OC_VA(0x69B574u)))
  #endif
#else
extern sai_path *sai_PathPoolBuf;
#endif
#define dword_69B574 (reinterpret_cast<DWORD_PTR>(sai_PathPoolBuf))

// CUnit +0x13C and +0x144, two of the four collision-bound indices
// CUnitBounds.cpp reaches by offset for the same reason: CUnit.h still spells
// the range `_unmapped_138[0x18]`.
#define UNIT_BOUND_LEFT(u) (*reinterpret_cast<int *>(reinterpret_cast<BYTE *>(u) + offsetof(CUnit, _unmapped_138) + 0x04))
#define UNIT_BOUND_TOP(u)  (*reinterpret_cast<int *>(reinterpret_cast<BYTE *>(u) + offsetof(CUnit, _unmapped_138) + 0x0C))

// The bodies, in oracle VA order.

// Mixes six fields into the save-file identity hash. The step is rotate-then-
// xor and the leading rotate of a zero seed is what folds the first xor away.
int CUnit::GetHash()
{
  unsigned int h = 0;

  h = _rotl(h ^ wType, 3);
  h = _rotl(h ^ lShields, 3);
  h = _rotl(h ^ wEnergy, 3);
  h = _rotl(h ^ PosS(pSprite->wPosX), 3);
  h = _rotl(h ^ pSprite->wPosY, 3);
  h = _rotl(h ^ ((lHitPoints + 0xFF) >> 8), 3);

  return h;
}

// Rewrites one AI back-pointer slot as (type << 16) | index. The switch's
// default arm shares the store, which is where the zeroed index comes from.
void FixupAIPointer(intptr_t nArg0)
{
  DWORD_PTR *pdwSlot = reinterpret_cast<DWORD_PTR *>(nArg0);
  void *pNode = reinterpret_cast<void *>(*pdwSlot);
  if (pNode == NULL)
    return;

  int nType = *(reinterpret_cast<BYTE *>(pNode) + offsetof(TGuard, bType));
  int nId = 0;

  switch (nType)
  {
    case 1:
      nId = guard_id(static_cast<TGuard *>(pNode));
      break;
    case 2:
      nId = worker_id(static_cast<TWorker *>(pNode));
      break;
    case 3:
      nId = bldg_id(static_cast<TBldg *>(pNode));
      break;
    case 4:
      nId = soldier_id(static_cast<TSoldier *>(pNode));
      break;
  }

  *pdwSlot = (nType << AiLinkShift()) | nId;  // Hyperion: 24 past 65,535 records
}

// Turns every saved id back into a pointer. The mirror of EncodeToSave, arm
// for arm; the unit-type tests are in the oracle's order.
void CUnit::DecodeFromSave(int nArg0)
{
  DWORD_PTR dwPath;
  DWORD_PTR dwNode;

  reinterpret_cast<CTarget *>(this)->PostLoad(0);

  if (UNIT_BOUND_TOP(this) == 1)
  {
    UNIT_BOUND_TOP(this) = -1;
    TrackCollision();
  }

  pPlayerUnitPrev = CUnit::FromId((DWORD)reinterpret_cast<DWORD_PTR>(pPlayerUnitPrev));
  pPlayerUnitNext = CUnit::FromId((DWORD)reinterpret_cast<DWORD_PTR>(pPlayerUnitNext));
  subunit = CUnit::FromId((DWORD)reinterpret_cast<DWORD_PTR>(subunit));
  pOrderQueueHead = COrder::GetOrder((int)reinterpret_cast<intptr_t>(pOrderQueueHead));
  pOrderQueueTail = COrder::GetOrder((int)reinterpret_cast<intptr_t>(pOrderQueueTail));
  dwUnion7C = reinterpret_cast<DWORD_PTR>(CUnit::FromId((DWORD)dwUnion7C));
  pUnion80 = CUnit::FromId((DWORD)reinterpret_cast<DWORD_PTR>(pUnion80));

  if (dwFlags & 2)
  {
    // The temporary is what keeps the upgrade byte out of AL: the store of the
    // decoded pointer has to come after the byte load, not before.
    CUnit *pOwner = CUnit::FromId((DWORD)reinterpret_cast<DWORD_PTR>(pUnionC0));
    upgrade_id_t bUpgrade = bUpgradeType;

    pUnionC0 = pOwner;
    if (bUpgrade != 0x3D && bTargetUpgradeLevel == 0)
      bTargetUpgradeLevel = get_upgrade_level(bPlayer, bUpgrade) + 1;
  }

  if (gulUnitTypeFlags[wType] & 8)
  {
    pUnionC0 = CUnit::FromId((DWORD)reinterpret_cast<DWORD_PTR>(pUnionC0));
    pUnionC8_SavedOrderTarget =
        CUnit::FromId((DWORD)reinterpret_cast<DWORD_PTR>(pUnionC8_SavedOrderTarget));
  }

  if (wType == 0x48 || wType == 0x52 || wType == 0x53 || wType == 0x51)
  {
    pUnionC0 = CUnit::FromId((DWORD)reinterpret_cast<DWORD_PTR>(pUnionC0));
    pHangarNext = CUnit::FromId((DWORD)reinterpret_cast<DWORD_PTR>(pHangarNext));
  }

  if (wType == 0x49 || wType == 0x55)
  {
    pUnionC0 = CUnit::FromId((DWORD)reinterpret_cast<DWORD_PTR>(pUnionC0));
    pHangarNext = CUnit::FromId((DWORD)reinterpret_cast<DWORD_PTR>(pHangarNext));
    pUnionC8_SavedOrderTarget =
        CUnit::FromId((DWORD)reinterpret_cast<DWORD_PTR>(pUnionC8_SavedOrderTarget));
  }

  if (wType == 0x6C)
    pUnionD0 = CUnit::FromId((DWORD)reinterpret_cast<DWORD_PTR>(pUnionD0));

  if (wType == 0x86)
    pUnionD0 = CUnit::FromId((DWORD)reinterpret_cast<DWORD_PTR>(pUnionD0));

  if (wType == 1 || wType == 0x10 || wType == 0x64 || wType == 0x63 ||
      wType == 0x68)
    pUnionD0 = reinterpret_cast<CUnit *>(
        CThingy::GetDoodadThingy((int)reinterpret_cast<intptr_t>(pUnionD0)));

  if (wType == 0x9C)
    pUnionD0 = reinterpret_cast<CUnit *>(
        CSprite::GetSprite((int)reinterpret_cast<intptr_t>(pUnionD0)));

  if (gulUnitTypeFlags[wType] & 0x800)
    pUnionD4 = CUnit::FromId((DWORD)reinterpret_cast<DWORD_PTR>(pUnionD4));

  if (gulUnitTypeFlags[wType] & 0x2000)
    pUnionD4 = CUnit::FromId((DWORD)reinterpret_cast<DWORD_PTR>(pUnionD4));

  if (gulUnitTypeFlags[wType] & 8)
  {
    pUnionD0 = CUnit::FromId((DWORD)reinterpret_cast<DWORD_PTR>(pUnionD0));
    pUnionD4 = CUnit::FromId((DWORD)reinterpret_cast<DWORD_PTR>(pUnionD4));
    pUnionD8_HarvestNext =
        CUnit::FromId((DWORD)reinterpret_cast<DWORD_PTR>(pUnionD8_HarvestNext));
  }

  dwUnionEC = reinterpret_cast<DWORD_PTR>(CUnit::FromId((DWORD)dwUnionEC));

  if (!(gulUnitTypeFlags[wType] & 1))
  {
    pBurrowLinkPrev = CUnit::FromId((DWORD)reinterpret_cast<DWORD_PTR>(pBurrowLinkPrev));
    pBurrowLinkNext = CUnit::FromId((DWORD)reinterpret_cast<DWORD_PTR>(pBurrowLinkNext));
  }

  if (wType == 0x9C)
  {
    pPsiLinkPrev = CUnit::FromId((DWORD)reinterpret_cast<DWORD_PTR>(pPsiLinkPrev));
    pRallyUnit = CUnit::FromId((DWORD)reinterpret_cast<DWORD_PTR>(pRallyUnit));
  }

  if (IsRallyer())
    pRallyUnit = CUnit::FromId((DWORD)reinterpret_cast<DWORD_PTR>(pRallyUnit));

  dwPath = reinterpret_cast<DWORD_PTR>(pPath);
  pPath = reinterpret_cast<sai_path *>(dwPath == 0 ? 0 : dword_69B574 + dwPath);

  pIrradiateSource = CUnit::FromId((DWORD)reinterpret_cast<DWORD_PTR>(pIrradiateSource));

  dwNode = dwUnion134;
  if (dwNode != 0)
  {
    // Hyperion: the kind above 24 bits of the id past 65,535 records, as
    // FixupAIPointer wrote it; in 1.17's, the word above 16 (+0x136)
    const int nShift = AiLinkShift();
    int nId = static_cast<int>(dwNode & ((static_cast<DWORD_PTR>(1) << nShift) - 1));

    switch (static_cast<int>(static_cast<DWORD>(dwNode) >> nShift))
    {
      case 1:
        dwUnion134 = reinterpret_cast<DWORD_PTR>(guard_ptr(nId));
        break;
      case 2:
        dwUnion134 = reinterpret_cast<DWORD_PTR>(worker_ptr(nId));
        break;
      case 3:
        dwUnion134 = reinterpret_cast<DWORD_PTR>(bldg_ptr(nId));
        break;
      case 4:
        dwUnion134 = reinterpret_cast<DWORD_PTR>(soldier_ptr(nId));
        break;
    }
  }

  if (nArg0 != 0)
  {
    pPrev = CUnit::FromId((DWORD)reinterpret_cast<DWORD_PTR>(pPrev));
    pNext = CUnit::FromId((DWORD)reinterpret_cast<DWORD_PTR>(pNext));
  }
}

// Turns every pointer into a saved id. DecodeFromSave's mirror.
void CUnit::EncodeToSave(int nArg0)
{
  DWORD_PTR dwPath;

  if (UNIT_BOUND_LEFT(this) >= 0)
  {
    UntrackCollision();
    UNIT_BOUND_TOP(this) = 1;
  }

  pPlayerUnitPrev = reinterpret_cast<CUnit *>((intptr_t)pPlayerUnitPrev->GetUnitID());
  pPlayerUnitNext = reinterpret_cast<CUnit *>((intptr_t)pPlayerUnitNext->GetUnitID());
  subunit = reinterpret_cast<CUnit *>((intptr_t)subunit->GetUnitID());
  pOrderQueueHead =
      reinterpret_cast<COrder *>((intptr_t)COrder::GetOrderID(pOrderQueueHead));
  pOrderQueueTail =
      reinterpret_cast<COrder *>((intptr_t)COrder::GetOrderID(pOrderQueueTail));
  dwUnion7C = reinterpret_cast<CUnit *>(dwUnion7C)->GetUnitID();
  pUnion80 = reinterpret_cast<CUnit *>((intptr_t)pUnion80->GetUnitID());

  if (dwFlags & 2)
    pUnionC0 = reinterpret_cast<CUnit *>((intptr_t)pUnionC0->GetUnitID());

  if (gulUnitTypeFlags[wType] & 8)
  {
    pUnionC0 = reinterpret_cast<CUnit *>((intptr_t)pUnionC0->GetUnitID());
    pUnionC8_SavedOrderTarget =
        reinterpret_cast<CUnit *>((intptr_t)pUnionC8_SavedOrderTarget->GetUnitID());
  }

  if (wType == 0x48 || wType == 0x52 || wType == 0x53 || wType == 0x51)
  {
    pUnionC0 = reinterpret_cast<CUnit *>((intptr_t)pUnionC0->GetUnitID());
    pHangarNext = reinterpret_cast<CUnit *>((intptr_t)pHangarNext->GetUnitID());
  }

  if (wType == 0x49 || wType == 0x55)
  {
    pUnionC0 = reinterpret_cast<CUnit *>((intptr_t)pUnionC0->GetUnitID());
    pHangarNext = reinterpret_cast<CUnit *>((intptr_t)pHangarNext->GetUnitID());
    pUnionC8_SavedOrderTarget =
        reinterpret_cast<CUnit *>((intptr_t)pUnionC8_SavedOrderTarget->GetUnitID());
  }

  if (wType == 0x6C)
    pUnionD0 = reinterpret_cast<CUnit *>((intptr_t)pUnionD0->GetUnitID());

  if (wType == 0x86)
    pUnionD0 = reinterpret_cast<CUnit *>((intptr_t)pUnionD0->GetUnitID());

  if (wType == 1 || wType == 0x10 || wType == 0x64 || wType == 0x63 ||
      wType == 0x68)
    pUnionD0 = reinterpret_cast<CUnit *>((intptr_t)CThingy::GetDoodadThingyID(
        reinterpret_cast<CThingy *>(pUnionD0)));

  if (wType == 0x9C)
    pUnionD0 = reinterpret_cast<CUnit *>(
        (DWORD_PTR)CSprite::GetSpriteID(reinterpret_cast<CSprite *>(pUnionD0)));

  if (gulUnitTypeFlags[wType] & 0x800)
    pUnionD4 = reinterpret_cast<CUnit *>(
        (intptr_t)reinterpret_cast<CUnit *>(pUnionD4)->GetUnitID());

  if (gulUnitTypeFlags[wType] & 0x2000)
    pUnionD4 = reinterpret_cast<CUnit *>(
        (intptr_t)reinterpret_cast<CUnit *>(pUnionD4)->GetUnitID());

  if (gulUnitTypeFlags[wType] & 8)
  {
    pUnionD0 = reinterpret_cast<CUnit *>((intptr_t)pUnionD0->GetUnitID());
    pUnionD4 = reinterpret_cast<CUnit *>(
        (intptr_t)reinterpret_cast<CUnit *>(pUnionD4)->GetUnitID());
    pUnionD8_HarvestNext =
        reinterpret_cast<CUnit *>((intptr_t)pUnionD8_HarvestNext->GetUnitID());
  }

  dwUnionEC = reinterpret_cast<CUnit *>(dwUnionEC)->GetUnitID();

  if (!(gulUnitTypeFlags[wType] & 1))
  {
    pBurrowLinkPrev = reinterpret_cast<CUnit *>((intptr_t)pBurrowLinkPrev->GetUnitID());
    pBurrowLinkNext = reinterpret_cast<CUnit *>((intptr_t)pBurrowLinkNext->GetUnitID());
  }

  if (wType == 0x9C)
  {
    pPsiLinkPrev = reinterpret_cast<CUnit *>((intptr_t)pPsiLinkPrev->GetUnitID());
    pRallyUnit = reinterpret_cast<CUnit *>((intptr_t)pRallyUnit->GetUnitID());
  }

  if (IsRallyer())
    pRallyUnit = reinterpret_cast<CUnit *>((intptr_t)pRallyUnit->GetUnitID());

  dwPath = reinterpret_cast<DWORD_PTR>(pPath);
  if (dwPath != 0)
    dwPath -= dword_69B574;
  pPath = reinterpret_cast<sai_path *>(dwPath);

  pIrradiateSource = reinterpret_cast<CUnit *>((intptr_t)pIrradiateSource->GetUnitID());
  FixupAIPointer(reinterpret_cast<intptr_t>(&dwUnion134));

  if (nArg0 != 0)
  {
    pPrev = reinterpret_cast<CUnit *>((intptr_t)pPrev->GetUnitID());
    pNext = reinterpret_cast<CUnit *>((intptr_t)pNext->GetUnitID());
  }

  reinterpret_cast<CTarget *>(this)->PreSave(0);
}
