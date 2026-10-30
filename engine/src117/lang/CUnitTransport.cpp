// CUnitTransport.cpp -- the transport and bunker cargo layer: the eight-slot cargo array
// at CUnit +0xB0, the units.dat volume and capacity tables that bound it, the load and
// unload order dispatchers, and the AI transport search. Bodies are in oracle VA order.
//
// TU membership matters in both directions here, and three bodies pull three different
// ways. CUnit::ExitTransport carries __declspec(noinline) on its declaration: without it
// /Ob2 splices all 372 bytes into CUnit::DispatchUnload. CUnit::FindClosestTransport
// depends on CUnit::IsATransport being inlined -- its first three loop filters are that
// body, and only the real inline leaves the full-width BOOL materialisation behind. And
// CUnit::HasEmptyCargoSpace and CUnit::ExitTransport each write a callee's code out
// literally (CUnit::GetTotalOccupiedCargo and sub_438180) while those callees are also
// defined here as real functions. Re-check all three after a change to this TU.
//
// Every loop bound in this family is unsigned: `movzx` off a BYTE dat table plus an
// unsigned index gives the oracle's jz and jb, where int gives jle and jl.

#include "../h/CUnitTransport.h"
#include "../h/CUnit.h"
#include "../h/CFlingy.h"
#include "../h/CSprite.h"
#include "../h/CTarget.h"
#include "../h/CUnitCombat.h"
#include "../h/CUnitTSpells.h"
#include "../h/CUnitZSpells.h"
#include "../h/point16.h"
#include "../h/order_target.h"
#include "../h/sai.h"
#include "../h/saveload.h"
#include "../h/SAI_Banker.h"
#include "../h/TAI_Captain.h"
#include "../h/TrigEnum.h"
#include "../gen/globals117.h"
#include "../h/handicap.h"  // Hyperion: a player's handicap

// The globals this TU reaches are generated rather than spelled here: every row that
// touches one is marked, so src117/gen/globals117.h declares all seven (gubUnitVolume,
// gubUnitCargoHold, gulUnitTypeFlags, gubUnitOrderGuard, gxUnitLife, gbUnitHasShields,
// guwUnitEnergy). A new row authored here dual-spells its own, one #ifndef per name.

// 0x4381D0 (23 B). An indexed cargo-slot read handed straight to CUnit::FromIdAlive, with
// the result returned unchanged.
CUnit *CUnit::GetTransportUnit(int nIndex)
{
  return CUnit::FromIdAlive(this->wCargoUnitIds[nIndex]);
}

// 0x438230 (33 B) -- shape: two units.dat BYTE tables compared, `sbb/inc`.
BOOL CUnit::CanFitInTransport(CUnit *pTransport)
{
  return gubUnitCargoHold[pTransport->wType] >= gubUnitVolume[this->wType];
}

// 0x438260 (48 B) -- shape: fixed eight-slot walk with an early TRUE exit.
BOOL CUnit::IsUnitInside()
{
  for (unsigned int i = 0; i < 8; i++)
  {
    if (CUnit::FromIdAlive(this->wCargoUnitIds[i]))
      return TRUE;
  }
  return FALSE;
}

// 0x438350 (53 B) -- shape: fixed eight-slot walk accumulating a table value.
int CUnit::GetTotalOccupiedCargo()
{
  int nTotal = 0;
  for (int i = 0; i < 8; i++)
  {
    CUnit *pUnit = CUnit::FromIdAlive(this->wCargoUnitIds[i]);
    if (pUnit)
      nTotal += gubUnitVolume[pUnit->wType];
  }
  return nTotal;
}

// 0x438140 (58 B) -- shape: capacity-bounded walk returning the first hit.
CUnit *CUnit::GetFirstTransportUnit()
{
  unsigned int nCapacity = gubUnitCargoHold[this->wType];
  for (unsigned int i = 0; i < nCapacity; i++)
  {
    CUnit *pUnit = CUnit::FromIdAlive(this->wCargoUnitIds[i]);
    if (pUnit)
      return pUnit;
  }
  return NULL;
}

// 0x437850 (58 B) -- shape: order dispatcher, three tail jmps to
// CUnit::EndDispatchOrder.
void CUnit::DispatchLoadUnitBunker()
{
  CUnit *pTarget = this->pOrderTarget;
  if (!pTarget)
  {
    this->EndDispatchOrder();
    return;
  }
  if (!pTarget->CanEnterTransport(this))
  {
    this->EndDispatchOrder();
    return;
  }
  pTarget->SetOrder_CUnit(0x5C, this);
  this->EndDispatchOrder();
}

// 0x437DA0 (85 B) -- shape: capacity-bounded walk invoking a callback.
BOOL CUnit::IterateTransport(TransportProc pfnProc, CUnit *pContext)
{
  unsigned int nCapacity = gubUnitCargoHold[this->wType];
  for (unsigned int i = 0; i < nCapacity; i++)
  {
    CUnit *pUnit = CUnit::FromIdAlive(this->wCargoUnitIds[i]);
    if (pUnit && pfnProc(pUnit, pContext))
      return TRUE;
  }
  return FALSE;
}

// 0x437440 (242 B) -- the cargo-fit predicate.
//
// The shared `return FALSE` must be the last statement, not the first guard's then-arm.
// Written as eight sequential `if (bad) return FALSE;` guards, MSVC makes the first
// guard's fail block the fall-through and every later guard reaches it with a 2-byte
// backward jne: 28 bytes short of the oracle's seven rel32 guards, with nothing else
// moved. One && chain whose else-path is the function's trailing `return FALSE` puts the
// block back at the end and MSVC lengthens the jumps by itself.
BOOL CUnit::CanEnterTransport(CUnit *pTarget)
{
  if ((pTarget->dwFlags & 1) && !(pTarget->dwFlags & 0x400) &&
      !pTarget->bLockdownTimer && !pTarget->bStasisTimer &&
      !pTarget->bParalyzeTimer && !(this->dwFlags & 0x10) &&
      this->bPlayer == pTarget->bPlayer &&
      (!(gulUnitTypeFlags[pTarget->wType] & 1) ||
       (this->GetRace() == 1 && gubUnitVolume[this->wType] <= 1u)))
  {
    unsigned int nCapacity = gubUnitCargoHold[pTarget->wType];
    unsigned int nTotal = 0;
    for (unsigned int i = 0; i < nCapacity; i++)
    {
      CUnit *pUnit = CUnit::FromIdAlive(pTarget->wCargoUnitIds[i]);
      if (pUnit)
        nTotal += gubUnitVolume[pUnit->wType];
    }
    return nCapacity - nTotal >= gubUnitVolume[this->wType];
  }
  return FALSE;
}

// 0x437B50 (66 B) -- order dispatcher.
void CUnit::DispatchPickUp()
{
  CUnit *pTarget = this->pOrderTarget;
  if (pTarget && pTarget->CanEnterTransport(this))
  {
    this->SetNextOrder_CUnit(0x5E, pTarget);
    this->ManageQueuedOrders();
    return;
  }
  if (this->dwUnion134)
    this->SetOrder(0x9C);
}

// 0x438180 (75 B) -- free __fastcall: place pUnit at pAnchor's sprite position.
int sub_438180(CUnit *pAnchor, CUnit *pUnit, mpoint_t *pOutput)
{
  mpoint_t  pt;
  CSprite *pSprite = pAnchor->pSprite;
  pt.x = pSprite->wPosX;
  pt.y = pSprite->wPosY;
  pUnit->SetUnitPosition(pSprite->wPosX, pSprite->wPosY);
  return FindPlaceForUnit(pUnit, &pt, pOutput,
                          NULL, 0, 0) != 0;
}

// 0x4381F0 (61 B) -- is this unit a transport?
BOOL CUnit::IsATransport()
{
  if (this->dwFlags & 0x40000000)
    return FALSE;
  if (this->wType == 0x2A && !get_upgrade_level(this->bPlayer, 0x18))
    return FALSE;
  return gubUnitCargoHold[this->wType] != 0;
}

// 0x438290 (82 B) -- eight-slot walk through each worker's +0xC0 link.
BOOL CUnit::IsFlagInside()
{
  for (unsigned int i = 0; i < 8; i++)
  {
    CUnit *pUnit = CUnit::FromIdAlive(this->wCargoUnitIds[i]);
    if (pUnit && (gulUnitTypeFlags[pUnit->wType] & 8))
    {
      CUnit *pCarried = pUnit->pUnionC0;
      if (pCarried && pCarried->wType == 0xD7)
        return TRUE;
    }
  }
  return FALSE;
}

// 0x4382F0 (81 B) -- the accumulate walk, written out literally rather than calling
// CUnit::GetTotalOccupiedCargo. `unsigned int nTotal`, not int: the tail is
// `cmp ebx, eax / sbb eax, eax / neg eax`, the unsigned below-idiom, and a signed
// accumulator makes the compare signed and costs that form.
BOOL CUnit::HasEmptyCargoSpace()
{
  unsigned int nTotal = 0;
  for (int i = 0; i < 8; i++)
  {
    CUnit *pUnit = CUnit::FromIdAlive(this->wCargoUnitIds[i]);
    if (pUnit)
      nTotal += gubUnitVolume[pUnit->wType];
  }
  return nTotal < gubUnitCargoHold[this->wType];
}

// 0x438390 (64 B) -- eight-slot walk testing gulUnitTypeFlags bit 3.
BOOL CUnit::TransportHasWorker()
{
  for (unsigned int i = 0; i < 8; i++)
  {
    CUnit *pUnit = CUnit::FromIdAlive(this->wCargoUnitIds[i]);
    if (pUnit && (gulUnitTypeFlags[pUnit->wType] & 8))
      return TRUE;
  }
  return FALSE;
}

// 0x4376D0 (198 B) -- the "walk into a transport" order dispatcher. Five early exits, all
// reaching one tail block that is a tail jmp into CUnit::EndDispatchOrder, which is what
// makes this void.
//
// CUnit::IsATransport (0x4381F0) is emitted inline here, so this body depends on that
// inline, and it is still spelled as a call: the `setnz cl / test ecx, ecx` is a
// materialised boolean that only an inlined BOOL-returning callee leaves behind, where
// `gubUnitCargoHold[...] != 0` written into the && chain folds to a bare cmp/jz.
//
// The shared exit is textually last, and that is the lever: as five separate
// `if (...) { EndDispatchOrder(); return; }` guards MSVC duplicates the tail five times.
// One `if (ok) { ...; return; }` with the call after it leaves a single tail at the end,
// which is what makes the oracle's three far guards rel32 and the near two rel8. The
// middle guard's nesting is read off the targets: the flags test guards only the two
// calls, and the bOrderState store belongs to the outer pair.
void CUnit::DispatchEnterTransport()
{
  CUnit *pTarget = this->pOrderTarget;

  if (pTarget != NULL && pTarget->IsATransport() &&
      this->CanEnterTransport(pTarget))
  {
    if (this->bOrderState == 0 && pTarget->bOrderId != 0x5E)
    {
      if (pTarget->dwFlags & 0x20000)
      {
        pTarget->SetNextOrder_CUnit(0x5E, this);
        pTarget->ManageQueuedOrders();
      }
      this->bOrderState = 1;
    }

    {
      CSprite *pSpr = pTarget->pSprite;
      reinterpret_cast<CFlingy *>(this)->SetDestination(
          PosS(pSpr->wPosX), pSpr->wPosY);
    }
    this->ApproachMoveTarget_CUnit(pTarget);
    if (this->IsWithinTargetRange(1, pTarget))
      this->EnterTransport(pTarget);
    return;
  }
  this->EndDispatchOrder();
}

// 0x437BA0 (198 B) -- the "unload everything" order dispatcher. A BYTE-counted eight-slot
// walk that calls CUnit::ExitTransport on every live passenger, then a three-way decision
// on whether anything was aboard and whether the last exit succeeded.
//
// The counter is a BYTE: an unsigned int counter indexes with the register directly and
// loses the `movzx eax, bl`. The two register-save regions are one if -- EBX and ESI are
// pushed inside the guarded region, so the early return is a plain return and not a jump
// to the epilogue -- and the post-loop decision is one `else if (bExited)` whose redundant
// test MSVC skips on the path that has already proved ESI non-zero.
//
// ExitTransport is a real call here, and intermediate shapes of this body had /Ob2
// splicing its 372 bytes in instead. Its declaration carries __declspec(noinline) for
// that reason; re-check the pair after a change to either.
void CUnit::DispatchUnload()
{
  BOOL bHadCargo;
  BOOL bExited;
  BYTE i;

  if (!(this->dwFlags & 2) && this->bUnion54 != 0)
    return;

  bHadCargo = FALSE;
  bExited = FALSE;
  for (i = 0; i < 8; i++)
  {
    CUnit *pUnit = CUnit::FromIdAlive(this->wCargoUnitIds[i]);
    if (pUnit != NULL)
    {
      bHadCargo = TRUE;
      bExited = pUnit->ExitTransport();
      if (!(this->dwFlags & 2))
        break;
    }
  }

  if (bHadCargo && !bExited)
  {
    if (this->dwUnion134 != 0 && !(this->dwFlags & 2))
    {
      tai_soldier_unload_into_neighbor(this);
      return;
    }
  }
  else if (bExited)
  {
    if (!(this->dwFlags & 2))
    {
      // A pointer walk with its own counter: the oracle keeps the cursor in EBX and the
      // trip count in ESI, which an indexed subscript does not reproduce.
      unit_id_t *pwSlot;  // Hyperion: WORD in 1.17
      unsigned int n;
      for (n = 0, pwSlot = this->wCargoUnitIds; n < 8; n++, pwSlot++)
      {
        if (CUnit::FromIdAlive(*pwSlot) != NULL)
          return;
      }
    }
  }
  this->EndDispatchOrder();
}

// 0x437E00 (372 B) -- put `this` back on the map out of its transport.
//
// sub_438180 (0x438180, 75 B) is written out here rather than called: its whole content --
// build a point from the anchor sprite's (+0x14, +0x16), call CUnit::SetUnitPosition with
// the same pair, then FindPlaceForUnit with three zeroes -- is this body's offsets
// 66..131, and the callees the listing names are sub_438180's own. BOOL: two exits are
// `mov eax, 1h` and one is `xor eax, eax`, and CUnit::DispatchUnload consumes the result.
BOOL CUnit::ExitTransport()
{
  CUnit *pTransport = this->pUnion80;

  if (pTransport == NULL)
    return FALSE;

  {
    int bAlreadyOut = (pTransport->dwFlagsVolatile >> 1) & 1;

    if ((pTransport->bUnion54 == 0 || bAlreadyOut) &&
        !(pTransport->dwFlags & 0x400) &&
        pTransport->bLockdownTimer == 0 &&
        pTransport->bStasisTimer == 0 &&
        pTransport->bParalyzeTimer == 0)
    {
      mpoint_t  pt;
      mpoint_t  ptOut;  // Hyperion: a map point

      {
        CSprite *pSpr = pTransport->pSprite;
        pt.x = pSpr->wPosX;
        pt.y = pSpr->wPosY;
        this->SetUnitPosition(pSpr->wPosX, pSpr->wPosY);
      }
      if (FindPlaceForUnit(this, &pt, &ptOut, NULL, 0, 0))
      {
        CUnit *pSub;
        WORD wType;
        int nWeapon;

        this->pUnion80->bUnion54 = 0x0F;
        this->SetUnitPosition(ptOut.x, ptOut.y);
        pSub = this->subunit;
        if (pSub != NULL)
          pSub->SetUnitPosition(ptOut.x, ptOut.y);
        this->BreakAnim();
        this->AbandonTransport(0);
        this->SetOrder(this->dwUnion134 != 0 ? 0x9C
                                             : gubUnitOrderGuard[this->wType]);
        status_set_update();

        if (!bAlreadyOut)
        {
          wType = this->wType;
          if (wType != 0x53)
          {
            nWeapon = this->GetWeaponGndEx();
            if (nWeapon != WEAPON_NONE)
              this->_unmapped_55[0] =
                  this->GetWeaponCD(static_cast<weapon_id_t>(nWeapon));
            nWeapon = gubUnitWeaponAir[wType];
            if (nWeapon != WEAPON_NONE)
              this->_unmapped_55[1] =
                  this->GetWeaponCD(static_cast<weapon_id_t>(nWeapon));
            this->_unmapped_55[2] = 0x1E;
            return TRUE;
          }
          this->bUnion54 = 0x1E;
        }
        return TRUE;
      }
    }
  }
  return FALSE;
}

// 0x437890 (362 B) -- CUnit::DispatchMoveUnload.
void CUnit::DispatchMoveUnload()
{
  mpoint_t  ptOut;  // Hyperion: a map point

  if (this->bOrderState == 0)
  {
    CUnit *pTarget = this->pOrderTarget;

    if (pTarget != NULL)
    {
      this->bOrderState = 1;
      this->wOrderTargetX = pTarget->pSprite->wPosX;
      this->wOrderTargetY = pTarget->pSprite->wPosY;
      this->pOrderTarget = NULL;
    }
  }

  {
    int nY = this->wOrderTargetY;
    int nX = this->wOrderTargetX;

    if (reinterpret_cast<CTarget *>(this)->XYInRadius(0x10, nX, nY))
    {
      this->SetNextOrder(0x6F);
      this->ManageQueuedOrders();
      return;
    }
  }

  this->SetDestination_xy(this->wOrderTargetX, this->wOrderTargetY);

  if (this->dwUnion134 == 0)
    return;

  {
    int nTargetY = this->wOrderTargetY;
    int nTargetX = this->wOrderTargetX;
    CSprite *pSprite = this->pSprite;
    upos_t wSpriteY = pSprite->wPosY;
    upos_t wSpriteX = pSprite->wPosX;
    int nSpriteHeight = get_map_height(PosS(wSpriteX), PosS(wSpriteY));
    int nTargetHeight = get_map_height(nTargetX, nTargetY);

    if (nSpriteHeight != nTargetHeight)
      return;
  }

  {
    int nY = this->wOrderTargetY;
    int nX = this->wOrderTargetX;

    if (!reinterpret_cast<CTarget *>(this)->XYInRadius(0xCA0, nX, nY))
      return;
  }

  if (!tai_captain_unit_connected_to_xy(this, this->wOrderTargetX, this->wOrderTargetY))
    return;

  if (this->TransportHasWorker())
    return;

  {
    int nY = this->wOrderTargetY;
    int nX = this->wOrderTargetX;
    BOOL bNear =
        reinterpret_cast<CTarget *>(this)->XYInRadius(0x200, nX, nY) != 0;

    for (unsigned int i = 0; i < 8; i++)
    {
      CUnit *pUnit = CUnit::FromIdAlive(this->wCargoUnitIds[i]);

      if (pUnit != NULL)
      {
        if (pUnit->wType == 0x53 && bNear == 0)
          return;
        if (!sub_438180(this, pUnit, &ptOut))
          return;
      }
    }
  }

  this->SetNextOrder(0x6F);
  this->ManageQueuedOrders();
}

// 0x437C70 (298 B) -- CUnit::EnterTransport. `this` is the unit boarding; the
// argument is the transport (its wType bounds the capacity walk and its
// wCargoUnitIds is what gets filled).
void CUnit::EnterTransport(CUnit *pTransport)
{
  unsigned int i;
  unsigned int nCapacity = gubUnitCargoHold[pTransport->wType];

  for (i = 0; i < nCapacity; i++)
  {
    if (!CUnit::FromIdAlive(pTransport->wCargoUnitIds[i]))
      break;
  }

  gamesnd_play(pTransport->GetRace() + 0x28, pTransport, 1, 0);
  town_clear_repair_worker(this);
  this->pUnion80 = pTransport;
  pTransport->wCargoUnitIds[i] = (unit_id_t)this->GetUnitID();  // Hyperion: (WORD) in 1.17
  this->dwFlags |= 0x40;
  this->Hide();
  this->pSprite->SetAnimation(0x0C, 1);
  status_set_update();

  if (pTransport->dwFlags & 2)
  {
    this->dwFlags = (this->dwFlags & 0xFFFDFFFF) | 0x20;
    this->ResetMovementState();

    if (this->subunit != NULL && (gulUnitTypeFlags[this->subunit->wType] & 0x10))
    {
      CUnit *pSub;

      this->subunit->SetUnitPosition(pTransport->pSprite->wPosX,
                                     pTransport->pSprite->wPosY);
      pSub = this->subunit;
      pSub->dwFlags = (pSub->dwFlags & 0xFFFDFFFF) | 0x20;
      pSub->ResetMovementState();
    }
    else
    {
      this->SetUnitPosition(pTransport->pSprite->wPosX,
                            pTransport->pSprite->wPosY);
    }
  }
}

// 0x437F80 (436 B) -- CUnit::FindClosestTransport.
BOOL __fastcall CUnit::FindClosestTransport(CUnit **ppBestTransport)
{
  CUnit *pUnit;
  DWORD dwBest;
  int nFound;

  *ppBestTransport = NULL;
  nFound = 0;
  dwBest = 0x5F5E0FF;

  for (pUnit = CUnit::GetPlayerUnitList(this->bPlayer); pUnit != NULL;
       pUnit = pUnit->pPlayerUnitNext)
  {
    unsigned int nType;

    if (!pUnit->IsATransport())
      continue;

    nType = pUnit->wType;
    if (!(pUnit->dwFlags & 1))
      continue;

    if (!(pUnit->dwFlags & 0x20000))
      continue;

    if (pUnit->pOrderTarget == this)
    {
      *ppBestTransport = pUnit;
      return TRUE;
    }

    {
      int nMax = HandicapLife(gxUnitLife[nType], pUnit->bPlayer) >> 8;  // Hyperion: at the owner's handicap

      if (nMax == 0)
      {
        int nCur = (pUnit->lHitPoints + 0xFF) >> 8;

        nMax = 1;
        if (nCur != 0)
          nMax = nCur;
      }

      if (((pUnit->lHitPoints + 0xFF) >> 8) < nMax * 3 / 4)
      {
        if (gbUnitHasShields[nType] == 0)
          continue;
        if (((LONG)pUnit->lShields >> 8) != HandicapShields(guwUnitEnergy[nType], pUnit->bPlayer))
          continue;
      }
    }

    nFound = 1;
    if (!this->CanEnterTransport(pUnit))
      continue;

    if (pUnit->bOrderId == 0x5E || pUnit->bOrderId == 0x6F)
      continue;

    {
      upos_t wTgtY = pUnit->wOrderTargetY;
      upos_t wTgtX = pUnit->wOrderTargetX;
      int nRegionA = GetRegion_xy(PosS(wTgtX), PosS(wTgtY));
      upos_t wMyY = this->wOrderTargetY;
      upos_t wMyX = this->wOrderTargetX;
      int nRegionB = GetRegion_xy(PosS(wMyX), PosS(wMyY));

      if (pUnit->bOrderId == 0x70 && nRegionA != nRegionB)
        continue;
    }

    {
      int nMyX = *reinterpret_cast<int *>(this->_unmapped_2C);
      CSprite *pSprite = pUnit->pSprite;
      upos_t wSpriteY = pSprite->wPosY;
      upos_t wSpriteX = pSprite->wPosX;
      DWORD dwDist = veclen(nMyX, PosS(wSpriteX),
                            *reinterpret_cast<int *>(this->_unmapped_2C + 4),
                            PosS(wSpriteY));

      if (dwDist < dwBest)
      {
        *ppBestTransport = pUnit;
        dwBest = dwDist;
      }
    }
  }

  return nFound;
}

// 0x437A00 (323 B) -- CUnit::DispatchLoadUnit.
//
// The three `pOrderTarget = NULL; return;` exits are written out separately;
// cross-jumping then keeps the merged block inline after the CanEnterTransport
// test, where the oracle has it. One shared `||` exit sinks it to the end.
void CUnit::DispatchLoadUnit()
{
  CUnit *pTransport;

  if (this->bPlayer == 0x0B)
  {
    this->SetOrder((this->dwFlags & 0x40) ? 0x17
                                          : gubUnitOrderGuard[this->wType]);
    return;
  }

  if (this->dwFlags & 0x40)
    return;

  if (this->AIPickAttackTarget(0, 0, 1))
    return;

  pTransport = this->pOrderTarget;
  if (pTransport == NULL)
  {
    if (!this->FindClosestTransport(&this->pOrderTarget))
    {
      this->EndDispatchOrder();
      return;
    }
    pTransport = this->pOrderTarget;
    if (pTransport == NULL)
      return;
  }
  else
  {
    if (!pTransport->IsATransport())
    {
      this->pOrderTarget = NULL;
      return;
    }
    if (pTransport->pOrderTarget != this)
    {
      this->pOrderTarget = NULL;
      return;
    }
  }

  if (!this->CanEnterTransport(pTransport))
  {
    this->pOrderTarget = NULL;
    return;
  }

  if (this->IsWithinTargetRange(1, pTransport))
  {
    this->EnterTransport(pTransport);
    pTransport->SetOrder_xy(0x70, this->wOrderTargetX, this->wOrderTargetY);
    if (pTransport->dwUnion134 != 0)
    {
      pTransport->SetOrder_xy_interrupt(6, PosS(pTransport->pSprite->wPosX),
                                        PosS(pTransport->pSprite->wPosY), 0);
    }
  }
  else
  {
    CSprite *pSprite;
    upos_t wSpriteY;
    upos_t wSpriteX;

    pTransport->SetNextOrder_CUnit(0x5E, this);
    pTransport->ManageQueuedOrders();

    pSprite = pTransport->pSprite;
    wSpriteY = pSprite->wPosY;
    wSpriteX = pSprite->wPosX;

    if (tai_captain_unit_connected_to_xy(this, PosS(wSpriteX), PosS(wSpriteY)))
    {
      reinterpret_cast<CFlingy *>(this)->SetDestination(
          PosS(pTransport->pSprite->wPosX), PosS(pTransport->pSprite->wPosY));
      this->ApproachMoveTarget_CUnit(pTransport);
    }
  }
}

// 0x437540 (391 B) -- the AI's "can I get there by transport?" move handler. Two exit
// blocks, and which one a guard reaches is read off the target: the first two guards jump
// to the block that pops neither EBX nor EDI, because both are pushed inside the guarded
// region, while every later failure reaches the one that pops both. DispatchUnload and
// AbandonTransport have the same shape.
BOOL CUnit::ComputerTransport(int nX, int nY)
{
  order_id_t bOrder = this->bOrderId;
  DWORD dwFlags;
  CUnit *pBestTransport;
  CUnit *pSub;
  int nOrderId;

  if (bOrder == 0x60)
    return TRUE;

  dwFlags = this->dwFlags;
  if ((dwFlags & 4) || (gulUnitTypeFlags[this->wType] & 0x10))
    return FALSE;
  if (PosS(this->pSprite->wPosX) == nX &&
      PosS(this->pSprite->wPosY) == nY)
    return FALSE;
  if (!(dwFlags & 0x20000))
    return FALSE;

  if (this->wType == 7 && bOrder == 0x22)
  {
    CUnit *pOrderTarget = this->pOrderTarget;
    if (pOrderTarget != NULL && (pOrderTarget->dwFlags & 4))
      return FALSE;
  }

  if (IsInaccessibleRegion(GetRegion_xy(nX, nY)))
    return FALSE;

  if (UnitCanReachGroundTarget_xy(this, nX, nY))
  {
    if (tai_soldier_captain_is_attacking(this, 1))
      return FALSE;
    if (tai_captain_unit_connected_to_xy(this, nX, nY))
    {
      CSprite *pSpr;
      upos_t wSpriteY;
      upos_t wSpriteX;

      if (this->wType != 0x53)
        return FALSE;
      pSpr = this->pSprite;
      wSpriteY = pSpr->wPosY;
      wSpriteX = pSpr->wPosX;
      if (veclen(nX, PosS(wSpriteX), nY,
                 PosS(wSpriteY)) < 0x3C0)
        return FALSE;
    }
  }

  if (this->FindClosestTransport(&pBestTransport))
  {
    nOrderId = this->bOrderId;
    if (this->pOrderQueueHead != NULL)
    {
      this->PrependOrder_Target(
          nOrderId, this->OrderTarget(),
          this->pOrderQueueHead);
    }
    else
    {
      this->SetOrder_Target_interrupt(
          nOrderId, this->OrderTarget(),
          FALSE);
    }
    this->PrependOrder_xy(0x60, nX, nY, this->pOrderQueueHead);

    pSub = this->subunit;
    if (this->IsTurret(pSub))
      pSub->SetOrder(0x17);
    this->EndUninterruptableDispatchOrder();
    this->HoldPosition();
    return TRUE;
  }
  return FALSE;
}

// 0x437320 (278 B) -- detach `this` from the transport it is riding in.
// The subunit tail is written out in both arms: one shared tail after the join
// makes MSVC retire EBX before it, where the oracle pops it in the epilogue.
// nArg0 stays `int`; it is only NULL-tested, and the other [ebp+08h] reads are
// a spill of this->pUnion80 into the dead slot.
void CUnit::AbandonTransport(int nArg0)
{
  if (!(this->dwFlags & 0x40))
    return;

  if (nArg0 != 0 && this->pUnion80 == NULL)
  {
    this->dwFlags &= ~0x60;
    this->ApplyPrototypeFlags();
    this->ResetMovementState();
    CUnit *pSubunit = this->subunit;
    if (pSubunit != NULL && (gulUnitTypeFlags[pSubunit->wType] & 0x10))
    {
      pSubunit->dwFlags &= ~0x20;
      pSubunit->ApplyPrototypeFlags();
      pSubunit->ResetMovementState();
    }
  }
  else
  {
    unit_id_t wId = static_cast<unit_id_t>(this->GetUnitID());  // Hyperion: WORD in 1.17
    CUnit *pTransport = this->pUnion80;
    unsigned int nCapacity = gubUnitCargoHold[pTransport->wType];
    unsigned int i;

    for (i = 0; i < nCapacity; i++)
    {
      if (pTransport->wCargoUnitIds[i] == wId)
        break;
    }

    if (nArg0 == 0)
    {
      gamesnd_play(pTransport->GetRace() + 0x2B, pTransport, 1, 0);
      this->Show();
    }
    else
    {
      CSprite *pSpr = pTransport->pSprite;
      this->SetUnitPosition(pSpr->wPosX, pSpr->wPosY);
    }

    this->pUnion80->wCargoUnitIds[i] = 0;
    this->dwFlags &= ~0x60;
    this->pUnion80 = NULL;
    this->ApplyPrototypeFlags();
    this->ResetMovementState();
    CUnit *pSubunit = this->subunit;
    if (pSubunit != NULL && (gulUnitTypeFlags[pSubunit->wType] & 0x10))
    {
      pSubunit->dwFlags &= ~0x20;
      pSubunit->ApplyPrototypeFlags();
      pSubunit->ResetMovementState();
    }
  }
}
