// CUnitTerran.cpp -- the terran lift and land, cloak and scanner, addon and silo family.
// Bodies are in ascending oracle VA order, which declaration order makes observable under
// /Gy and /Ox.

#include "oc_data.h"  // Hyperion: OC_VA
#include "../h/playervision.h"  // Hyperion: the slots past 7
#include "platform.h"
#include "CUnit.h"
#include "CSprite.h"
#include "CFlingy.h"
#include "rect16.h"
#include "CUnitTerran.h"
#include "TrigEnum.h"
#include "CImage.h"
#include "point16.h"
// collide.h (reached through CUnitBuild.h) declares mtx_query_place_bldg with
// the body's int nType; this TU calls it through the WORD view below, so the
// header's view is renamed away.
#define mtx_query_place_bldg mtx_query_place_bldg_int_view
#include "CUnitBuild.h"
#include "CUnitInit.h"
#include "CUnitTSpells.h"
#include "count.h"
#include "order.h"
#include "CTarget.h"
#include "statbtn.h"
#include "CThingy.h"
#include "order_target.h"
#include "TAI_Captain.h"
#include "COrder.h"
#include "gamesnd.h"
#include "collide.h"
#include "placebox.h"
#include "../h/selectlimits.h"  // Hyperion: SelectionWide
#include "../h/handicap.h"  // Hyperion: a player's handicap
#undef mtx_query_place_bldg

// The unit type goes out unconverted, as in CUnitZerg.cpp's view.
// Hyperion: the definition (collide.cpp) reads the cell and the type as
// ints, and this TU's callers pass them as 16 bits. The view takes the
// definition's types, which the x64 build needs, and the macro narrows the
// arguments as 1.17's did.
extern "C" int __fastcall mtx_query_place_bldg(CUnit *pUnit, BYTE bPlayer, int sX, int sY, int nType, int nSlot, int nArg6, int nArg7, int nArg8);
#define mtx_query_place_bldg(a0, a1, a2, a3, a4, a5, a6, a7, a8) (mtx_query_place_bldg)((a0), (a1), (short)(a2), (short)(a3), (WORD)(a4), (a5), (a6), (a7), (a8))

// This TU owns dual spellings for every fixed-address operand it reaches.
// Registered names keep the registry spelling. The three unregistered names
// are sheet/owned-corpus names and remain TU-local, following CUnitBuild.cpp's
// established gUnitBoundBox precedent; no shared registry change is needed.
#ifdef OC_DLL_MODE
  #ifndef gulUnitTypeFlags
  #define gulUnitTypeFlags (reinterpret_cast<DWORD *>(OC_VA(0x521660u)))
  #endif
  #ifndef gubUnitOrderGuard
  #define gubUnitOrderGuard (reinterpret_cast<BYTE *>(OC_VA(0x520F20u)))
  #endif
  #ifndef gUnitBoundBox
  #define gUnitBoundBox (reinterpret_cast<rect16_t *>(OC_VA(0x522C38u)))
  #endif
  #ifndef gpImageUnit
  #define gpImageUnit (*reinterpret_cast<CUnit **>(OC_VA(0x543300u)))
  #endif
  #ifndef gxUnitLife
  #define gxUnitLife (reinterpret_cast<LONG *>(OC_VA(0x520708u)))
  #endif
  #ifndef techdata_energy_g
  #define techdata_energy_g (reinterpret_cast<WORD *>(OC_VA(0x51F868u)))
  #endif
  #ifndef gUnitPlaceboxSize
  #define gUnitPlaceboxSize (reinterpret_cast<point16_t *>(OC_VA(0x522728u)))
  #endif
  #ifndef gUnitAddOnOffset
  #define gUnitAddOnOffset (reinterpret_cast<point16_t *>(OC_VA(0x522AB8u)))
  #endif
  #ifndef gubUnitFlingy
  #define gubUnitFlingy (reinterpret_cast<BYTE *>(OC_VA(0x51FAA8u)))
  #endif
  #ifndef guwFlingySprite
  #define guwFlingySprite (reinterpret_cast<WORD *>(OC_VA(0x510AE0u)))
  #endif
  #ifndef guwSpriteImage
  #define guwSpriteImage (reinterpret_cast<WORD *>(OC_VA(0x51EAA8u)))
  #endif
  #ifndef gubUnitFaceAngle
  #define gubUnitFaceAngle (reinterpret_cast<BYTE *>(OC_VA(0x520370u)))
  #endif
  #ifndef gubUnitPriorityPlane
  #define gubUnitPriorityPlane (reinterpret_cast<BYTE *>(OC_VA(0x520A98u)))
  #endif
  #ifndef dword_665D44
  #define dword_665D44 (*reinterpret_cast<DWORD *>(OC_VA(0x665D44u)))
  #endif
  #ifndef byte_5264C2
  #define byte_5264C2 (*reinterpret_cast<BYTE *>(OC_VA(0x5264C2u)))
  #endif
  #ifndef g_LocalNationID
  #define g_LocalNationID (*reinterpret_cast<DWORD *>(OC_VA(0x508D50u)))
  #endif
  #ifndef byte_6AB4C0
  #define byte_6AB4C0 (*reinterpret_cast<BYTE *>(OC_VA(0x6AB4C0u)))
  #endif
#else
extern BYTE     byte_6AB4C0;
extern BYTE     byte_5264C2;
extern DWORD    g_LocalNationID;
extern DWORD    gulUnitTypeFlags[228] OC_UNALIGNED;
extern order_id_t gubUnitOrderGuard[228] OC_UNALIGNED;
extern rect16_t gUnitBoundBox[228] OC_UNALIGNED;
extern CUnit   *gpImageUnit;
extern DWORD    dword_665D44;
extern LONG     gxUnitLife[228] OC_UNALIGNED;
extern WORD     techdata_energy_g[44] OC_UNALIGNED;
extern point16_t gUnitPlaceboxSize[228] OC_UNALIGNED;
extern point16_t gUnitAddOnOffset[96] OC_UNALIGNED;  // indexed [wType - 106]
extern flingy_id_t gubUnitFlingy[228] OC_UNALIGNED;
extern WORD     guwFlingySprite[209] OC_UNALIGNED;
extern WORD     guwSpriteImage[517];
extern BYTE     gubUnitFaceAngle[228] OC_UNALIGNED;
extern BYTE     gubUnitPriorityPlane[228] OC_UNALIGNED;
#endif

void CUnit::AlignWithTurretBase() {
  reinterpret_cast<CFlingy *>(this)->BindFacingDirection(
      subunit->bUnion21, 0x40);
}

void CUnit::CancelNuke() {
  CUnit *pNuke = pUnion80;
  if (pNuke != NULL && pNuke->wType == 0x0E) {
    pUnion80 = NULL;
    pNuke->pUnion80 = NULL;
  }
}

BOOL CUnit::BuildingCrushTank() {
  mrect_t  area;
  upos_t nPosX = pSprite->wPosX;
  upos_t nPosY = pSprite->wPosY;
  area.top = PosS(nPosY - gUnitBoundBox[wType].top);
  area.bottom = PosS(nPosY + gUnitBoundBox[wType].bottom);
  area.left = PosS(nPosX - gUnitBoundBox[wType].left);
  area.right = PosS(nPosX + gUnitBoundBox[wType].right);
  return find_foreach_unit(&area, reinterpret_cast<FindUnitCb>(crush_tank_cb),
                           this) != NULL;
}

void CUnit::BuildingLandCrushBurrowed() {
  mrect_t  area;
  upos_t nPosX = pSprite->wPosX;
  upos_t nPosY = pSprite->wPosY;
  area.top = PosS(nPosY - gUnitBoundBox[wType].top);
  area.bottom = PosS(nPosY + gUnitBoundBox[wType].bottom);
  area.left = PosS(nPosX - gUnitBoundBox[wType].left);
  area.right = PosS(nPosX + gUnitBoundBox[wType].right);
  sub_442B60(&area, DestroyBurrowed, this);
}

int __fastcall DestroyBurrowed(CUnit *pUnit, CUnit *pBuilding) {
  if (pUnit != pBuilding &&
      ((!pUnit->IsEnemyUnit(pBuilding) &&
        !pUnit->CanMove() &&
        pUnit->CanAttack()) ||
       (!pUnit->IsEnemyUnit(pBuilding) && (pUnit->dwFlags & 0x10)))) {
    pUnit->Suicide();
  }
  return 0;
}

void CUnit::DetachAddon() {
  CUnit *pDetached = pAddon;
  pDetached->GiveToNeutralSlot();
  pAddon = NULL;
  status_set_update();
  gamesnd_do_liftoff(pDetached);

  CUnit *pSavedImageUnit = gpImageUnit;
  gpImageUnit = pDetached;
  pDetached->pSprite->SetAnimation(0x12, 1);
  gpImageUnit = pSavedImageUnit;
}

// oracle 0x435BF0 (763 B).
// A lifted building's landing order: descend onto the spot, claim its tiles,
// touch down, then sort out the order queue and any addon it lands beside.
// The type is read twice, for the pushed argument and for the size index, which
// gives the oracle's index in EAX and the pushed copy in EDX.
void CUnit::DispatchBuildingLand() {
  if (dwFlags & 2) {
    order_target_t target = OrderTarget();
    SetOrder(0x48);
    SetOrder_Target_interrupt(0x47, target, 0);
    SetOrder_interrupt(gubUnitOrderGuard[wType], 0);
    return;
  }
  switch (bOrderState) {
  case 0: {
    char cOffset = pSprite->GetEffectOffset();
    SetDestination_xy(wOrderTargetX, wOrderTargetY - cOffset);
    bOrderState = 1;
    return;
  }
  case 1: {
    if (!AtMoveTarget())
      return;
    WORD wUnitType = wType;
    WORD wIdx = wType;
    short nHeight = gUnitPlaceboxSize[wIdx].y;
    short nWidth = gUnitPlaceboxSize[wIdx].x;
    if (mtx_query_place_bldg(this, bPlayer, PosS((wOrderTargetX - nWidth / 2) / 32),
                             PosS((wOrderTargetY - nHeight / 2) / 32), wUnitType,
                             0, 0, 1, 0)) {
      gamesnd_land_error(this);
      if (pOrderQueueHead != NULL && GetFirstQueuedOrder() == 0x24)
        RemovePreviousOrder();
      EndDispatchOrder();
      return;
    }
    PlaceBldg(wOrderTargetX, wOrderTargetY);
    bBuildingPlaced = 1;
    SetDestination_xy(wOrderTargetX, wOrderTargetY);
    pSprite->sub_416970();
    dwFlags |= 0x1000;
    lTopSpeed = 0x100;
    reinterpret_cast<CFlingy *>(this)->ForceSpeed(0x100);
    bOrderState = 2;
    return;
  }
  case 2: {
    BYTE bDir = bDirection;
    upos_t wY = pSprite->wPosY;
    upos_t wX = pSprite->wPosX;
    const short pNext[2] = {PosS(wDestinationX), PosS(wDestinationY)};  // Hyperion: the destination by name (stage E)
    int nDiff = bDir - AngleBetween(PosS(wX), PosS(wY), pNext[0], pNext[1]);
    if (!reinterpret_cast<CFlingy *>(this)->AtDestination() && nDiff != 0)
      return;
    pSprite->SetAnimation(0x11, 1);
    bOrderState = 3;
  }
  case 3: {
    if (!AtMoveTarget())
      return;
    if (!(bOrderFlags & 0x10))
      return;
    bOrderFlags &= ~0x10;
    UnplaceBldg(wOrderTargetX, wOrderTargetY);
    UntrackCollision();
    MakeLanded();
    pSprite->bElevationLevel = 4;
    ToggleFlyingStatus(0);
    SetUnitPosition(wOrderTargetX, wOrderTargetY);
    TrackCollision();
    // Hyperion: a building stays in a selection of several in Hyperion's
    if ((pSprite->flags.b & 8) && byte_6AB4C0 > 1 && !SelectionWide())
      sub_4A7B80();
    pSprite->SetShadowOffset(0);
    pSprite->sub_417840();
    pSprite->DoLandingDust();
    status_set_update();
    bBuildingPlaced = 0;
    BuildingLandCrushBurrowed();
    COrder *pOrder = pOrderQueueHead;
    if (pOrder != NULL) {
      switch (pOrder->bOrderId) {
      case 6:
      case 0x31:
        PrependOrder(0x48, pOrder);
        break;
      default: {
        COrder *pKeep = pOrder->bOrderId == 0x24 ? pOrder : NULL;
        while (pOrderQueueTail != pKeep)
          RemoveQueuedOrder(pOrderQueueTail);
        if (pOrderQueueHead == NULL)
          SetOrder(gubUnitOrderGuard[wType]);
      }
      }
    }
    sub_4370E0();
    EndUninterruptableDispatchOrder();
    CUnit *pAddon = sub_431AE0();
    if (pAddon != NULL)
      sub_4359C0(pAddon);
    if (placebox_is_on()) {
      sub_47D530();
      if (!sub_47DB60())
        set_placebox(0, NULL, 1);
    }
    return;
  }
  }
}

// CUnit::AtMoveTarget's body; the oracle inlines it into the lift-off and
// medic handlers.
static int AtMoveTargetInline(CUnit *pUnit) {
  CSprite *pSprite = pUnit->pSprite;
  if (pUnit->wMoveTargetX == pSprite->wPosX &&
      pUnit->wMoveTargetY == PosU(pSprite->wPosY))
    return (pUnit->dwFlags & 0x80000) ? 2 : 1;
  return 0;
}

void CUnit::DispatchBuildingLiftoff() {
  if ((dwFlags & 2) && (gulUnitTypeFlags[wType] & 0x20)) {
    if (pAddon != NULL)
      DetachAddon();

    if (IsRallyer())
      pRallyUnit = this;

    UntrackCollision();
    MakeLifted();

    pSprite->bElevationLevel = 0x0C;
    dwFlags |= 4;
    ResetMovementState();
    TrackCollision();
    pSprite->sub_416970();
    status_set_update();

    reinterpret_cast<CFlingy *>(this)->SetDestination(
        PosS(pSprite->wPosX), pSprite->wPosY - 42);

    if (wType == 0x82)
      sub_4B47F0(this);
    if (placebox_is_on())
      sub_47D530();

    bOrderId = 0x4A;
    return;
  }

  if (pOrderQueueHead == NULL)
    SetOrder_interrupt(gubUnitOrderGuard[wType], 0);
  EndUninterruptableDispatchOrder();
}

// A tail jump.
void CUnit::DispatchCloak() {
  sub_4340D0();
}

void CUnit::DispatchDecloak() {
  SetBackgroundOrder(0x17);
}

// Lift a Terran building off: face the move target, set the raised destination,
// start the lift animation, then restore speed and shadow on arrival.
void CUnit::DispatchLiftOff() {
  switch (bOrderState) {
  case 0: {
    BYTE bDir = bDirection;
    upos_t wY = pSprite->wPosY;
    upos_t wX = pSprite->wPosX;
    const short pNext[2] = {PosS(wDestinationX), PosS(wDestinationY)};  // Hyperion: the destination by name (stage E)
    int nDiff = bDir - AngleBetween(PosS(wX), PosS(wY), pNext[0], pNext[1]);
    if ((WORD)pNext[0] != wPositionX || (WORD)pNext[1] != wPositionY) {
      if (nDiff != 0)
        return;
    }
  }
  case 1: {
    int nLift = (gulUnitTypeFlags[wType] & 8) ? 7 : 0x2A;
    // The dead zero between nLift and wY makes y the add's destination.
    char cOff = 0;
    upos_t wY = pSprite->wPosY;
    cOff = pSprite->GetEffectOffset();
    SetDestination_xy(PosS(pSprite->wPosX), PosS(wY) + cOff - nLift);
    bOrderState = 2;
    return;
  }
  case 2:
    pSprite->SetAnimation(0x12, 1);
    bOrderState = 3;
    pSprite->DoLiftoffDust();
    lTopSpeed = 0x100;
    reinterpret_cast<CFlingy *>(this)->ForceSpeed(0x100);
  case 3:
    if (!AtMoveTargetInline(this))
      return;
    if (!(bOrderFlags & 0x10))
      return;
    bOrderFlags &= ~0x10;
    if (!(gulUnitTypeFlags[wType] & 1))
      lTopSpeed = FlingyMaxSpeedWithModifiers();
    pSprite->SetShadowOffset((gulUnitTypeFlags[wType] & 8) ? 7 : 0x2A);
    pSprite->sub_417840();
    EndUninterruptableDispatchOrder();
    return;
  }
}

void CUnit::DispatchMedicHealMove() {
  CUnit *pTarget = pOrderTarget;
  if (pTarget != NULL) {
    if (sub_437060(pTarget)) {
      SetOrder(0xB0);
      reinterpret_cast<CTarget *>(this)->SetFocusTarget(pTarget);
      SetMoveTarget_CUnit(pTarget);
      return;
    }
    wOrderTargetX = pTarget->pSprite->wPosX;
    wOrderTargetY = pTarget->pSprite->wPosY;
  }
  if (!ApproachMoveTarget_xy(wOrderTargetX, wOrderTargetY))
    return;
  if (AtMoveTargetInline(this)) {
    EndDispatchOrder();
    return;
  }
  if (bUnion54 != 0)
    return;
  bUnion54 = 0x0F;
  CUnit *pHeal = FindHealTarget();
  if (pHeal == NULL)
    return;
  int x = wOrderTargetX;
  int y = wOrderTargetY;
  SetOrder_Target(0xB0, OrderTarget());
  SetMoveTarget_CUnit(pHeal);
  reinterpret_cast<CTarget *>(this)->SetFocusTarget(pHeal);
  if (pTarget != NULL)
    AppendOrder_CUnit(0xB1, pTarget, NULL);
  else
    AppendOrder_xy(0xB1, x, y, NULL);
}

void CUnit::DispatchMedicReturnToIdle() {
  if ((bMoveFlags & 2) == 0)
    pSprite->SetAnimation(0x0C, 1);
  EndUninterruptableDispatchOrder();
}

// The nuke's own launch order: climb out of the silo, announce the launch,
// hide while it travels, then drop onto the target and detonate.
void CUnit::DispatchNukeLaunch() {
  BYTE bState = bOrderState;
  if (bState < 5 && pUnion80 == NULL) {
    Suicide();
    return;
  }
  switch (bState) {
  case 0:
    gamesnd_play(0x54, this, 1, 0);
    SetDestination_xy(PosS(pSprite->wPosX), gUnitBoundBox[0x0E].top);
    bUnion54 = 0x5A;
    bOrderState = 1;
    break;
  case 1:
    if (bUnion54 > 0x2D && !AtMoveTarget())
      break;
    gamesnd_play(byte_5264C2 + 0x7F, NULL, 1, 0);
    map_msg_target_self(status_get_str(0x417), g_LocalNationID);
    bOrderState = 2;
    break;
  case 2:
    if (bUnion54 != 0 && !AtMoveTarget())
      break;
    Hide();
    pUnion80->pUnion80 = this;
    Stop();
    bOrderState = 3;
    break;
  case 3:
    if (bMoveFlags & 2)
      break;
    pSprite->SetAnimation(0x15, 1);
    bOrderState = 4;
    break;
  case 4: {
    if (!(bOrderFlags & 2))
      break;
    bOrderFlags &= ~2;
    int nY = gUnitBoundBox[0x0E].top > wOrderTargetY - 0x140
                 ? gUnitBoundBox[0x0E].top : wOrderTargetY - 0x140;
    upos_t nX = wOrderTargetX;
    if (gUnitBoundBox[0x0E].left > PosS(nX))
      nX = gUnitBoundBox[0x0E].left;
    SetUnitPosition(nX, nY);
    reinterpret_cast<CFlingy *>(this)->FaceDirection(0x80);
    SetDestination_xy(wOrderTargetX, wOrderTargetY);
    Show();
    bOrderState = 5;
    break;
  }
  case 5: {
    int nY = PosS(wMoveTargetY);
    int nX = PosS(wMoveTargetX);
    if (!reinterpret_cast<CTarget *>(this)->XYInRadius(10, nX, nY))
      break;
    pOrderTarget = this;
    pSprite->SetAnimation(0x0D, 1);
    bOrderState = 6;
    break;
  }
  case 6:
    if (!(bOrderFlags & 1))
      break;
    bOrderFlags &= ~1;
    SelfDestruct();
    break;
  }
}

// The ghost's nuke-paint order: re-issue it on the unit, else on the spot.
void CUnit::DispatchNukePaint() {
  if (pOrderTarget != NULL)
    SetOrder_CUnit(0x7F, pOrderTarget);
  else
    SetOrder_xy(0x80, wOrderTargetX, wOrderTargetY);
}

// The ghost's nuke-guiding order: raise the dot, then wait for the missile.
// The player goes out as one byte written into a dword argument: the oracle
// stores it to [ebp-4] and pushes the whole slot, which is what gives this
// body its frame.
void CUnit::DispatchNukeTrack() {
  switch (bOrderState) {
  case 0: {
    pSprite->SetAnimation(0x0D, 1);
    union { BYTE b; int n; } owner;
    owner.b = bPlayer;
    pUnionD0 = reinterpret_cast<CUnit *>(CThingy::AllocThingy(
        0xE7, pUnion80->wOrderTargetX, pUnion80->wOrderTargetY, owner.n));
    if (pUnionD0 != NULL) {
      pUnionD0->pSprite->bElevationLevel = pSprite->bElevationLevel + 1;
      reinterpret_cast<CThingy *>(pUnionD0)->UpdateVisibility();
    }
    bOrderState = 6;
    break;
  }
  case 6: {
    CUnit *pNuke = pUnion80;
    if (pNuke != NULL && pNuke->bOrderState != 5)
      break;
    sub_4372F0();
    SetButtonset(wType);
    status_set_update();
    pSprite->SetAnimation(0x0C, 1);
    if (pUnionD0 != NULL) {
      pUnionD0->pSprite->SetAnimation(1, 1);
      pUnionD0 = NULL;
    }
    AIMoveWhereNeeded(this);
    break;
  }
  }
}

// The vulture's lay-mine order: walk to the spot, then place a spider mine.
void CUnit::DispatchPlaceMine() {
  if (bOrderState == 0) {
    if (pOrderTarget != NULL) {
      if (!SetDestination_CUnit(pOrderTarget))
        return;
      CUnit *pTarget = pOrderTarget;
      wOrderTargetX = pTarget->pSprite->wPosX;
      wOrderTargetY = pTarget->pSprite->wPosY;
      bOrderState = 1;
      return;
    }
    if (!SetDestination_xy(wOrderTargetX, wOrderTargetY))
      return;
    bOrderState = 1;
    return;
  }
  if (!TechUseAllowed(3, bPlayer, this) || AtMoveTargetInline(this) == 2) {
    EndDispatchOrder();
    return;
  }
  int nY = wOrderTargetY;
  int nX = wOrderTargetX;
  if (!reinterpret_cast<CTarget *>(this)->XYInRadius(0x14, nX, nY)) {
    if (dwUnion134 == 0)
      return;
    if (!AtMoveTarget())
      return;
    EndDispatchOrder();
    return;
  }
  StopUnitMovement();
  mpoint_t  pos;
  pos.x = wOrderTargetX;
  pos.y = wOrderTargetY;
  ClipToMap(&pos, 0x0D);
  CUnit *pMine = CreateUnitAt(0x0D, pos.x, pos.y, bPlayer);
  if (pMine == NULL) {
    ShowCreateError(bPlayer);
    EndDispatchOrder();
    return;
  }
  pMine->FinishedMaking();
  reinterpret_cast<CFlingy *>(pMine)->FaceDirection(bUnion21);
  bSpiderMineCount--;
  EndDispatchOrder();
}

void CUnit::DispatchPlaceScanner() {
  WORD wCost = techdata_energy_g[4] << 8;
  if (NeedsMoreEnergy(wCost)) {
    char *pszMsg = status_get_str(count_race(wType) + 0x360);
    map_msg_target_self(pszMsg, bPlayer);
    EndDispatchOrder();
    return;
  }
  mpoint_t  pos;
  pos.x = wOrderTargetX;
  pos.y = wOrderTargetY;
  ClipToMap(&pos, 0x21);
  CUnit *pScanner = CreateUnitAt(0x21, pos.x, pos.y, bPlayer);
  if (pScanner == NULL) {
    ShowCreateError(bPlayer);
    EndDispatchOrder();
    return;
  }
  pScanner->FinishedMaking();
  gamesnd_play(0x184, this, 1, 0);
  SubtractEnergy(techdata_energy_g[4] << 8);
  EndDispatchOrder();
}

void CUnit::DispatchScanner() {
  if ((bOrderFlags & 4) != 0) {
    bOrderFlags &= (BYTE)~4;
    Suicide();
  }
}

// oracle 0x436A80 (322 B).
// The type swap tests a second copy of the type and falls back to the first,
// as in DispatchTankMode.
void CUnit::DispatchSiegeMode() {
  switch (bOrderState) {
  case 0:
    if (wType != 0x05 && wType != 0x17) {
      EndDispatchOrder();
      return;
    }
    if (bMoveFlags & 2)
      StopUnitMovement();
    bOrderState = 1;
  case 1:
    if (bMoveFlags & 2)
      return;
    if (subunit->bMoveFlags & 2)
      return;
    if (subunit->dwFlags & 0x2000)
      return;
    reinterpret_cast<CFlingy *>(this)->SetSpawnFacing(
        reinterpret_cast<CFlingy::spawn_dir_t *>(gubUnitFaceAngle)[wType]);
    subunit->SetOrder_CUnit(0x18, this);
    reinterpret_cast<CFlingy *>(subunit)->SetSpawnFacing(
        reinterpret_cast<CFlingy::spawn_dir_t *>(gubUnitFaceAngle)[subunit->wType]);
    SetAnimTurret(0x0D);
    bOrderState = 2;
    if (BuildingCrushTank())
      return;
  case 2:
    if (!IsFacingMoveTarget())
      return;
    if (!subunit->IsFacingMoveTarget())
      return;
    {
      WORD wOld = wType;
      WORD nTest = wType;
      TransformType(nTest != 0x05 ? (nTest == 0x17 ? 0x19 : wOld) : 0x1E);
    }
    bOrderState = 3;
  case 3:
    if (!(bOrderFlags & 1))
      return;
    bOrderFlags &= ~1;
    if (pUnion74 != NULL && GetFirstQueuedOrder() != 0x64) {
      SetOrder(gubUnitOrderGuard[wType]);
      EndUninterruptableDispatchOrder();
      subunit->sub_4372F0();
      return;
    }
    sub_4372F0();
    subunit->sub_4372F0();
    return;
  }
}

// oracle 0x436BE0 (257 B).
// Both unit and turret end through an inlined sub_4372F0. The facing is read
// through the table as a spawn_dir_t lvalue; a built temporary costs a frame.
// The type swap tests a second copy of the type and falls back to the first,
// which keeps the two in separate registers as the oracle does.
void CUnit::DispatchTankMode() {
  switch (bOrderState) {
  case 0:
    if (wType != 0x1E && wType != 0x19) {
      EndDispatchOrder();
      return;
    }
    if (subunit->dwFlags & 0x2000)
      return;
    subunit->SetOrder_CUnit(0x18, this);
    reinterpret_cast<CFlingy *>(subunit)->SetSpawnFacing(
        reinterpret_cast<CFlingy::spawn_dir_t *>(gubUnitFaceAngle)[subunit->wType]);
    SetAnimTurret(0x0D);
    bOrderState = 2;
  case 2:
    if (!subunit->IsFacingMoveTarget())
      return;
    pSprite->SetAnimation(0x0E, 1);
    SetAnimTurret(0x0E);
    bOrderFlags &= ~1;
    bOrderState = 3;
  case 3:
    if (!(bOrderFlags & 1))
      return;
    WORD nType = wType;
    bOrderFlags &= ~1;
    WORD nTest = wType;
    TransformType(nTest != 0x19 ? (nTest == 0x1E ? 5 : nType) : 0x17);
    sub_4372F0();
    subunit->sub_4372F0();
    return;
  }
}

// The spider mine: burrow, wait for a target, unburrow and chase it.
void CUnit::DispatchVultureMine() {
  switch (bOrderState) {
  case 0:
    _unmapped_55[0] = 0x3C;
    bOrderState = 1;
  case 1:
    if (_unmapped_55[0] != 0)
      return;
    pSprite->SetAnimation(0x19, 1);
    dwFlags |= 0x200000;
    bOrderState = 2;
  case 2:
    if (!(bOrderFlags & 4))
      return;
    bOrderFlags &= ~4;
    if (!(dwFlags & 0x40000000)) {
      ReplaceWithBurrowedSprite();
      sub_41D300();
    }
    bOrderState = 3;
  case 3: {
    CUnit *pTarget = FindMineTarget();
    if (pTarget == NULL)
      return;
    pOrderTarget = pTarget;
    pSprite->SetAnimation(0x1A, 1);
    pSprite->flags.b &= ~0x40;
    SetBackgroundOrder(0x17);
    bOrderState = 4;
  }
  case 4:
    if (!(bOrderFlags & 4))
      return;
    bOrderFlags &= ~4;
    pSprite->bElevationLevel = gubUnitPriorityPlane[wType];
    dwFlags &= ~0x200000;
    if (pOrderTarget == NULL) {
      bOrderState = 1;
      return;
    }
    reinterpret_cast<CFlingy *>(this)->InterceptTarget(pOrderTarget);
    bOrderState = 5;
    return;
  case 5:
    MakeUnburrowed();
    if (pOrderTarget != NULL && IsWithinTargetRange(0x240, pOrderTarget)) {
      reinterpret_cast<CFlingy *>(this)->InterceptTarget(pOrderTarget);
      if (!IsWithinTargetRange(0x1E, pOrderTarget)) {
        if (AtMoveTarget() != 2)
          return;
      } else {
        pOrderTarget = NULL;
        wOrderTargetX = pSprite->wPosX;
        wOrderTargetY = pSprite->wPosY;
        pSprite->SetAnimation(0x0D, 1);
        bOrderState = 6;
        goto detonate;
      }
    }
    Stop();
    bOrderState = 1;
    return;
  case 6:
  detonate:
    if (!(bOrderFlags & 1))
      return;
    Suicide();
    return;
  }
}

// The offset is built in the parameter's home slot and pushed as one dword.
int CUnit::DisplayLandingOverlay(int nUnitType) {
  point16_t pos;
  pos.x = -(gUnitPlaceboxSize[wType].x / 2);
  pos.y = -(gUnitPlaceboxSize[wType].y / 2);
  pos.x -= gUnitAddOnOffset[wType - 106].x;
  pos.y -= gUnitAddOnOffset[wType - 106].y;
  pos.x += gUnitPlaceboxSize[(WORD)nUnitType].x / 2;
  pos.y += gUnitPlaceboxSize[(WORD)nUnitType].y / 2;
  WORD wImage = guwSpriteImage[guwFlingySprite[gubUnitFlingy[(WORD)nUnitType]]];
  return CImage::AllocPlacebox(pSprite, wImage,
                               *reinterpret_cast<DWORD *>(&pos),
                               nUnitType) != NULL;
}

int __fastcall sub_436F20(CUnit *pUnit, CUnit *pContext) {
  if (pUnit != pContext && !pUnit->CanMove() && pUnit->CanAttack() &&
      pUnit->bOrderId != 0) {
    dword_665D44 = 1;
    return 1;
  }
  return 0;
}

CUnit *CUnit::FindArmedSilo(int nFlags, CUnit *pExclude) {
  CUnit *pUnit = GetPlayerUnitList(bPlayer);
  while (pUnit != NULL) {
    if (pUnit->wType == 0x6C && pUnit->pUnionD4 != NULL &&
        (nFlags == 0 || pUnit->pUnionD0->pUnion80 == NULL ||
         pUnit->pUnionD0->pUnion80 == pExclude ||
         pUnit->pUnionD0->pUnion80->wType == 0x6C)) {
      return pUnit;
    }
    pUnit = pUnit->pPlayerUnitNext;
  }
  return NULL;
}

void CUnit::MakeLanded() {
  dwFlags &= ~0x30000u;
  dwFlags |= 2;
  ResetMovementState();
}

void CUnit::ToggleFlyingStatus(BYTE bFlying) {
  dwFlagsVolatile ^=
      (dwFlagsVolatile ^ (static_cast<DWORD>(bFlying) << 2)) & 4;
  ResetMovementState();
}

void CUnit::MakeLifted() {
  DWORD flags = (dwFlags & ~2u) | 0x10000u;
  dwFlags = flags;
  DWORD lifted = ~(gulUnitTypeFlags[wType] << 13);
  lifted = ((lifted ^ flags) & 0x20000u) ^ flags;
  dwFlags = lifted;
  ResetMovementState();
}

// A tail jump.
void CUnit::RemovePlaceboxSelf() {
  pSprite->KillPlacebox();
}

BOOL CUnit::sub_437060(CUnit *pCandidate) {
  if (pCandidate == NULL || pCandidate == this)
    return 0;
  if (dwFlags & 0x40000000)
    return 0;
  DWORD dwTypeFlags = gulUnitTypeFlags[pCandidate->wType];
  if (!(dwTypeFlags & 0x10000) || (dwTypeFlags & 1))
    return 0;
  if (pCandidate->dwFlags & 4)
    return 0;
  if (IsEnemyUnit(pCandidate))
    return 0;
  if (!PlayerIsLobby(pCandidate->bPlayer) || !PlayerIsLobby(bPlayer))  // Hyperion: >= 8 in 1.17
    return 0;
  if (pCandidate->lHitPoints >= HandicapLife(gxUnitLife[pCandidate->wType], pCandidate->bPlayer))  // Hyperion: at the owner's handicap
    return 0;
  return !pCandidate->IsDisabled();
}

// Proof-only -- oracle 0x4370E0 (156 B). Extern MATCH; shipped FIXUPSET hold.
void CUnit::sub_4370E0() {
  mrect_t  area;
  dword_665D44 = 0;
  upos_t nPosX = pSprite->wPosX;
  upos_t nPosY = pSprite->wPosY;
  area.top = PosS(nPosY - gUnitBoundBox[wType].top);
  area.bottom = PosS(nPosY + gUnitBoundBox[wType].bottom);
  area.left = PosS(nPosX - gUnitBoundBox[wType].left);
  area.right = PosS(nPosX + gUnitBoundBox[wType].right);
  sub_442B60(&area, sub_436F20, this);

  if (dword_665D44 != 0) {
    SetOrder(0x48);
    SetOrder_interrupt(gubUnitOrderGuard[wType], 0);
  }
}

void CUnit::sub_437180() {
  CUnit *pUnit = sub_431AE0();
  if (pUnit != NULL)
    sub_4359C0(pUnit);
}

// +0x107 is the target's being-healed byte, still inside _unmapped_106.
int CUnit::sub_4371A0(CUnit *pTarget) {
  if (!TechUseAllowed(0x22, bPlayer, this))
    return 0;
  if (pTarget == NULL || !sub_437060(pTarget))
    return 0;
  if (pTarget->_unmapped_106[1] != 0) {
    pTarget = FindHealTarget();
    if (pTarget == NULL)
      return 0;
    reinterpret_cast<CTarget *>(this)->SetFocusTarget(pTarget);
    SetMoveTarget_CUnit(pTarget);
  }
  if (!IsWithinTargetRange(0x1E, pTarget))
    return 3;
  DWORD dwHeal = HandicapLife(gxUnitLife[pTarget->wType], pTarget->bPlayer) - pTarget->lHitPoints;  // Hyperion: at the owner's handicap
  if (dwHeal > 200)
    dwHeal = 200;
  DWORD dwCost = dwHeal >> 1;
  if (NeedsMoreEnergy(dwCost))
    return 2;
  SubtractEnergy(dwCost);
  pTarget->ModifyLife(dwHeal);
  pTarget->_unmapped_106[1] = 1;
  return 1;
}

CUnit *CUnit::sub_437290() {
  CUnit *pUnit = GetUsedList();
  while (pUnit != NULL) {
    if (pUnit->pUnionC0 == this)
      return pUnit;
    pUnit = pUnit->pNext;
  }
  return NULL;
}

// One DWORD read of the flags feeds both tests. MSVC narrows the first to
// `test al, 2h` on its own, so the field stays a DWORD here even though the
// oracle emits the byte form. The three failure paths share one exit and it
// has to be textually last, or the first test returns inline instead.
int __fastcall crush_tank_cb(CUnit *pUnit, CUnit *pContext) {
  if (pUnit != pContext) {
    DWORD dwFlags = pUnit->dwFlags;
    if ((dwFlags & 2) != 0 && (dwFlags & 0x200000) == 0) {
      pContext->Suicide();
      return 1;
    }
  }
  return 0;
}

void CUnit::sub_4372F0() {
  if (pUnion74 == NULL)
    SetOrder_interrupt(gubUnitOrderGuard[wType], 0);
  EndUninterruptableDispatchOrder();
}
