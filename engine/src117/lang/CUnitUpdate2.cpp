// CUnitUpdate2.cpp -- the per-frame update core: RunOrders and the order runners, the
// movement step, the move-target and position primitives, and the per-unit timers.
// Bodies are in oracle VA order.
//
// The split from CUnitUpdate.cpp is part of the code: that file's bodies emit real calls
// to these primitives, and /Ob2 splices any callee defined in the same TU. The oracle's
// own bytes call them, so 1.17 had them in separate TUs too.
//
// The dual spellings below are TU-local and named off the sheet: the gpImageUnit and
// gpImageTargeter pair at 0x543300, order_unk7 (indexed by COrder::bOrderId),
// gUnitBoundBox (rect16_t[228]), and gbLocalMask, a BYTE view of the registered
// gdwLocalMask DWORD that CSprite::UpdateFlash byte-reads.
#include "oc_data.h"  // Hyperion: OC_VA
#include "../h/playervision.h"  // Hyperion: the slots past 7
#include "../h/playertables.h"  // Hyperion: player slots past 11
#include <string.h>

#include "../h/CUnitUpdate.h"
#include "../h/CUnit.h"
#include "../h/CUnitBuild.h"    // ours: ClipToMap(point16_t*, WORD)
#include "../h/CUnitCombat.h"
#include "../h/SAI_Main.h"        // ai_update_unit_spells
#include "../h/COrder.h"        // ours: COrder::bOrderId at +0x08
#include "../h/CUnitSearches.h" // ours: count_get_completion
#include "../h/CUnitTerran.h"   // ours: sub_442B60 + CUnitTerranFindCb + rect16_t
#include "../h/CUnitTSpells.h"
#include "../h/CUnitZSpells.h"  // ours: class CThingy (SpriteUpdate lives there)
#include "../h/CTarget.h"
#include "../h/CSprite.h"
#include "../h/CImage.h"         // GetOverlayOffset, SetOffset
#include "../h/CFlingy.h"
#include "../h/CThingy.h"       // ours: the wave-56 SpriteUpdate note
#include "../h/TrigAct.h"       // ours: CreateUnitAt (DispatchSpawningLarva)
#include "../h/saveload.h"
#include "../h/order.h"
#include "../h/TAI_Captain.h"     // tai_call_attackers_if_disabled, tai_call_attackers_when_hit
#include "../h/CBullet.h"         // CBullet::NextBucket (EntityUpdate)
#include "../h/net_cmds.h"        // PlayerData (EntityUpdate)
#include "../h/CUnitProtoss.h"    // UpdateProtossPowerGrid
#include "../h/CUnitSpells.h"     // CloakUpdate
#include "../gen/globals117.h"
#include "../h/typelimits.h"  // Hyperion: OrderTypeMod
#include "../h/selectlimits.h"  // Hyperion: SELECTION_MAX
#include "../h/handicap.h"  // Hyperion: a player's handicap

// EntityUpdate's order-sequence countdowns (CUnitInit.cpp's globals) and the
// fog flag, registered; one #ifndef per name.
#ifdef OC_DLL_MODE
  #ifndef sgdwOrderSeqDelay_g
  #define sgdwOrderSeqDelay_g (*reinterpret_cast<DWORD *>(OC_VA(0x5DA390u)))
  #endif
  #ifndef sgdwBackgroundOrderSeqDelay_g
  #define sgdwBackgroundOrderSeqDelay_g (*reinterpret_cast<DWORD *>(OC_VA(0x5DA394u)))
  #endif
  #ifndef byte_6663EC
  #define byte_6663EC (*reinterpret_cast<BYTE *>(OC_VA(0x6663ECu)))
  #endif
#else
extern DWORD sgdwOrderSeqDelay_g;
extern DWORD sgdwBackgroundOrderSeqDelay_g;
extern BYTE byte_6663EC;
#endif

// gbSelCircleColor_g -- 0x52904A, BYTE[12] per-player selection-circle
// colour; registered, not yet emitted into globals117.h.
#ifndef gbSelCircleColor_g
#ifdef OC_DLL_MODE
#define gbSelCircleColor_g (reinterpret_cast<BYTE *>(OC_VA(0x52904Au)))
#else
extern PlayerSelCircleColorTable gbSelCircleColor_g;
#endif
#endif

#ifdef OC_DLL_MODE
#ifndef gpImageUnit
#define gpImageUnit (*reinterpret_cast<CUnit **>(OC_VA(0x543300u)))
#endif
#ifndef gpImageTargeter
#define gpImageTargeter (*reinterpret_cast<CUnit **>(OC_VA(0x543304u)))
#endif
#ifndef order_unk7
#define order_unk7 (reinterpret_cast<const BYTE *>(OC_VA(0x51B2C0u)))
#endif
#ifndef gUnitBoundBox
#define gUnitBoundBox (reinterpret_cast<rect16_t *>(OC_VA(0x522C38u)))
#endif
#ifndef gbLocalMask
#define gbLocalMask (*reinterpret_cast<BYTE *>(OC_VA(0x672668u)))
#endif
#ifndef dword_6B6900
#define dword_6B6900 (*reinterpret_cast<DWORD *>(OC_VA(0x6B6900u)))
#endif
#ifndef dword_6C46D8
#define dword_6C46D8 (*reinterpret_cast<DWORD *>(OC_VA(0x6C46D8u)))
#endif
#ifndef byte_520A98
#define byte_520A98 (reinterpret_cast<const BYTE *>(OC_VA(0x520A98u)))
#endif
#ifndef gubUnitOrderGuard
#define gubUnitOrderGuard (reinterpret_cast<BYTE *>(OC_VA(0x520F20u)))
#endif
#ifndef byte_665D54
#define byte_665D54 (*reinterpret_cast<BYTE *>(OC_VA(0x665D54u)))
#endif
#else
extern CUnit *gpImageUnit;
extern CUnit *gpImageTargeter;
extern BYTE order_unk7[];
extern rect16_t gUnitBoundBox[228] OC_UNALIGNED;
extern BYTE gbLocalMask;
extern DWORD dword_6B6900;
extern DWORD dword_6C46D8;
extern const BYTE byte_520A98[];
// Hyperion: units.dat's guard order by its name and type, 16 bits on x64;
// a byte alias of it read the low or high byte of the wrong entry, Die (0) for a
// Lurker that unburrows
extern order_id_t gubUnitOrderGuard[228] OC_UNALIGNED;
extern BYTE byte_665D54;
#endif

// TU-local free-function spellings for the bodies that are only compiled, not shipped.
// An ECX-object callee with no member home is spelled free with the object as its first
// argument: under /Gr the pointer rides ECX and the call site is byte-identical. The
// moment a real member declaration exists in CUnit.h the free spelling yields to it, since
// inside a CUnit member the class name shadows a file-scope free declaration.
void town_move_morphed_building(CUnit *pMother, CUnit *pLarva);      // 0x486200
void unit_gui_reset();                               // nullary void
void HidePowerup(CSprite *pSprite);                  // tail-jmps CImage::Hide

// sub_4B47F0 (0x4B47F0, unported) is declared in CUnitTerran.h as
// `BOOL __fastcall sub_4B47F0(CUnit *pUnit)` and this TU calls that spelling. Do not
// re-declare it here: a second overload mangles differently and the thunk follows the
// header's spelling.

// find_first_unit_near (0x442B20, unported): the proximity search. The coordinates are
// spelled WORD here because DispatchSpawningLarva's call site emits 16-bit `mov cx/dx`,
// which an int parameter cannot produce. CBullet.h declares them int and is not on this
// TU's include path, so the two spellings never meet.
typedef int(__fastcall *pfn_larva_filter_t)(CUnit *pUnit);
CUnit *__fastcall find_first_unit_near(WORD x, WORD y, int nRadius,
                             pfn_larva_filter_t pfnFilter, int nUnused);
// The filter callback (0x43D910) has no listing; the signature is here to satisfy the
// callback type, and what the body proves is the baked address.
int __fastcall sub_43D910(CUnit *pUnit);

// Runs this unit's background order. Case bodies are in the oracle's code
// order, which is not case-value order.
void CUnit::RunBGOrders()
{
  order_id_t bOrder = bBackgroundOrderId;
  if (bOrder == 0x95)
    return DispatchHallucination();
  if ((dwFlags & 0x400) != 0)
    return;
  if (bLockdownTimer != 0)
    return;
  if (bStasisTimer != 0)
    return;
  if (bParalyzeTimer != 0)
    return;

  switch (bOrder) {
  case 0x25:
    return DispatchBuildAddon();
  case 0x6D:
    return DispatchCloak();
  case 0x83:
    return DispatchCloakNeighbors();
  case 0x6E:
    return DispatchDecloak();
  case 0x44:
    return DispatchShieldBattery();
  case 0x4E:
    return DispatchSpawningLarva();
  case 0x66:
    return DispatchSpreadCreep();
  case 0x26:
    return DispatchTrain();
  case 0x3F:
    return DispatchTrainFighter();
  }
}

// The two orders that call a folded empty member (0x4BE950) each get a spelling
// of their own; with one spelling MSVC merges the two arms.
void __fastcall voice_squelch(CUnit *);
void __fastcall voice_squelch(void *);

// Runs this unit's current order: death and warp-in first, nothing while it is
// disabled, the per-frame orders, then every eighth frame the rest. Case bodies
// are in the oracle's code order, read off its three index tables.
void CUnit::RunOrders() {
  switch (bOrderId) {
  case 0x30:
    return DispatchBuildSelfProtoss();
  case 0xAE:
    return DispatchProtossWarpIn();
  case 0x00:
    return DispatchDie();
  case 0x81:
    return DispatchNukeTrack();
  }
  if ((dwFlags & 0x400) != 0 || bLockdownTimer != 0 || bStasisTimer != 0 ||
      bParalyzeTimer != 0) {
    tai_call_attackers_if_disabled(this);
    return;
  }
  if ((dwFlags & 0x20000) == 0 && (dwFlags & 0x8000) != 0)
    tai_call_attackers_if_disabled(this);
  switch (bOrderId) {
  case 0x82:
    DispatchArbiter();
    break;
  case 0x47:
    DispatchBuildingLand();
    break;
  case 0x48:
    DispatchBuildingLiftoff();
    break;
  case 0x1E:
    DispatchBuild();
    break;
  case 0x1F:
    DispatchBuildProtoss();
    break;
  case 0x2C:
    DispatchBuildSelf();
    break;
  case 0x2D:
    DispatchBuildSelfZerg();
    break;
  case 0x21:
    DispatchConstructBuilding();
    break;
  case 0xA6:
    DispatchCritter();
    break;
  case 0x1A:
    DispatchDroneBuild();
    break;
  case 0x59:
    DispatchHarvest4();
    break;
  case 0x58:
    DispatchHarvestInterrupt();
    break;
  case 0xB3:
    DispatchMedicReturnToIdle();
    break;
  case 0x2A:
    DispatchMorph();
    break;
  case 0x7C:
    DispatchNukeTrain();
    break;
  case 0x9B:
    DispatchCTFCop2();
    break;
  case 0x42:
    DispatchRechargeUnit();
    break;
  case 0x22:
    DispatchRepair();
    break;
  case 0x4B:
    DispatchResearchTech();
    break;
  case 0xA5:
    Suicide();
    break;
  case 0x62:
    DispatchSiegeMode();
    break;
  case 0x5B:
    DispatchShieldBatteryInterrupt();
    break;
  case 0x69:
    DispatchSummonArchon();
    break;
  case 0x6A:
    DispatchSummonComplete();
    break;
  case 0x63:
    DispatchTankMode();
    break;
  case 0x41:
    DispatchTrilobyteStrafeUnit();
    break;
  case 0x16:
    DispatchTurretAttack();
    break;
  case 0x04:
    DispatchTurretGuard();
    break;
  case 0x96:
    DispatchUndoFlyer();
    break;
  case 0x97:
    DispatchUndoTransparentWorker();
    break;
  case 0x4C:
    DispatchUpgrade();
    break;
  case 0x29:
    DispatchZergBirth();
    break;
  case 0xB7:
    DispatchSummonDArchon();
    break;
  case 0xB0:
    DispatchMedicHeal();
    break;
  case 0xB1:
    DispatchMedicHealMove();
    break;
  case 0xB2:
    DispatchMedicHoldPos();
    break;
  }
  if (bUnion85-- != 0)
    return;
  bUnion85 = 8;
  tai_call_attackers_when_hit(this);
  bUnion92 = 0;
  switch (bOrderId) {
  case 0x6C: case 0x88:
    if (bOrderState == 0) {
      StopUnitMovement();
      bOrderState = 1;
    }
  case 0x17:
    if (pOrderQueueHead != 0)
      ManageQueuedOrders();
    return;
  case 0x9F:
    DispatchAIPatrol();
    return;
  case 0x0B:
    DispatchAttackFixedRange();
    return;
  case 0x0D:
    voice_squelch(this);
    return;
  case 0x0E:
    DispatchAttackMove();
    return;
  case 0x9D:
    DispatchAttackMoveEP();
    return;
  case 0x0C:
    // A second folded empty member: a distinct spelling keeps its own arm.
    voice_squelch(static_cast<void *>(this));
    return;
  case 0x0A:
    DispatchAttackUnit();
    return;
  case 0x43:
    DispatchRecharge();
    return;
  case 0x0F:
    DispatchDroneInfesting();
    return;
  case 0x74:
    DispatchBurrow();
    return;
  case 0x75:
    DispatchBurrowed();
    return;
  case 0x32: case 0x38: case 0x39:
    DispatchCarrier();
    return;
  case 0x37:
    DispatchCarrierIgnore();
    return;
  case 0x35:
    DispatchCarrierAttack();
    return;
  case 0x06: case 0x33:
    DispatchIgnoreMove();
    return;
  case 0x34:
    DispatchCarrierStop();
    return;
  case 0x94:
    DispatchHallucinate();
    return;
  case 0x07:
    DispatchTrilobyteStop();
    return;
  case 0x9C:
    DispatchComputerAI();
    return;
  case 0xA3:
    DispatchAIReturn();
    return;
  case 0x99:
    DispatchCTFCopInit();
    return;
  case 0x9A:
    DispatchCTFCop();
    return;
  case 0x76:
    DispatchDeburrow();
    return;
  case 0x8D:
    DispatchDefMatrix();
    return;
  case 0xA8:
    DispatchDoorOpen();
    return;
  case 0xA9:
    DispatchDoorClose();
    return;
  case 0xAD:
    DispatchDisableDoodad();
    return;
  case 0xAC:
    DispatchEnableDoodad();
    return;
  case 0x46:
    DispatchDroneLand();
    return;
  case 0x19:
    DispatchDroneStartBuild();
    return;
  case 0x2F:
    DispatchEnterNydus();
    return;
  case 0x5C:
    DispatchEnterTransport();
    return;
  case 0x5E:
    DispatchLoadUnitTransport();
    return;
  case 0x11:
    DispatchPowerupCTF();
    return;
  case 0x5F:
    DispatchLoadUnitBunker();
    return;
  case 0x9E:
    DispatchHarassMoveEP();
    return;
  case 0xA7:
    DispatchHiddenGun();
    return;
  case 0xAA:
    DispatchHideTrap();
    return;
  case 0xAB:
    DispatchRevealTrap();
    return;
  case 0x31:
    DispatchFollow();
    return;
  case 0x02:
    DispatchGuard();
    return;
  case 0x03:
    DispatchPlayerGuard();
    return;
  case 0xA0:
    DispatchGuardPost();
    return;
  case 0x4F: case 0x51:
    DispatchHarvest();
    return;
  case 0x52:
    DispatchGasMinePrecondition();
    return;
  case 0x53:
    DispatchInteractWithGasMine();
    return;
  case 0x55:
    DispatchHarvestMinerals();
    return;
  case 0x56:
    DispatchHarvestMineralsWait();
    return;
  case 0x57:
    DispatchHarvestingMinerals();
    return;
  case 0x6B:
    DispatchHoldPosition();
    return;
  case 0x65:
    DispatchCreepInit();
    return;
  case 0xA4:
    DispatchJuiceInit();
    return;
  case 0x4D:
    DispatchIdleLarva();
    return;
  case 0x4A:
    DispatchLiftOff();
    return;
  case 0x70:
    DispatchMoveUnload();
    return;
  case 0x60:
    DispatchLoadUnit();
    return;
  case 0xA2:
    DispatchNeutral();
    return;
  case 0x80:
    DispatchNukeGround();
    return;
  case 0x7D:
    DispatchNukeLaunch();
    return;
  case 0x7E:
    DispatchNukePaint();
    return;
  case 0x7F:
    DispatchNukeUnit();
    return;
  case 0x2E:
    DispatchMorphCanal();
    return;
  case 0x98:
    DispatchPatrol();
    return;
  case 0x5D:
    DispatchPickUp();
    return;
  case 0x24:
    DispatchPlaceAddon();
    return;
  case 0x84:
    DispatchPlaceMine();
    return;
  case 0x61:
    DispatchPowerup();
    return;
  case 0x20:
    DispatchPylonBuild();
    return;
  case 0x1B:
    DispatchInfest();
    return;
  case 0x1D:
    DispatchInfestEnd();
    return;
  case 0x85:
    DispatchRClick();
    return;
  case 0xA1:
    DispatchRescuePassive();
    return;
  case 0x45:
    DispatchReturn();
    return;
  case 0x54: case 0x5A:
    DispatchReturnResource();
    return;
  case 0x87:
    DispatchSapLocation();
    return;
  case 0x86:
    DispatchSapUnit();
    return;
  case 0x8B:
    DispatchPlaceScanner();
    return;
  case 0x8C:
    DispatchScanner();
    return;
  case 0x15:
    DispatchStayInRange();
    return;
  case 0x01:
    DispatchStop();
    return;
  case 0x67:
    DispatchStopSpreadCreep();
    return;
  case 0x40:
    DispatchStrafeUnit();
    return;
  case 0x89:
    DispatchTeleport();
    return;
  case 0x12:
    DispatchTowerGuard();
    return;
  case 0x13:
    DispatchTowerTarget();
    return;
  case 0x3A: case 0x3D: case 0x3E:
    DispatchTrilobyte();
    return;
  case 0x3B:
    DispatchTrilobyteAttack();
    return;
  case 0x6F:
    DispatchUnload();
    return;
  case 0x14:
    DispatchVultureMine();
    return;
  case 0x64:
    DispatchWatchTarget();
    return;
  case 0x2B:
    DispatchZergBldgMorph();
    return;
  case 0x71: case 0x73: case 0x77: case 0x78: case 0x79: case 0x7A: case 0x8E: case 0x8F: case 0x90: case 0x91: case 0x92: case 0x93: case 0xB4: case 0xB5: case 0xB9: case 0xBA:
    DispatchClickAbility();
    return;
  case 0x09: case 0x1C: case 0x23: case 0x36: case 0x3C: case 0x50: case 0x72:
    DispatchLClick();
    return;
  case 0xAF:
    DispatchMedic();
    return;
  case 0xB6:
    DispatchMindControl();
    return;
  case 0xB8:
    DispatchPsyFeedback();
    return;
  case 0xBB:
    DispatchJunkYardDog();
    return;
  default:
    // Hyperion: a mod's order is a spell, its weapon and tech its row's
    if (OrderTypeMod(bOrderId))
      DispatchClickAbility();
    return;
  }
}

// Runs the orders a loaded unit keeps, then every eighth frame its bunker and
// loading orders. Case bodies are in the oracle's code order.
// The SetOrder(5 or 0x17) arm is an 8-bit neg/sbb select only because
// SetOrder takes a BYTE; with an int parameter it lowers to cmov.
void CUnit::RunTransportOrders()
{
  switch (bOrderId) {
  case 0x00:
    return DispatchDie();
  case 0x03:
  case 0x04:
  case 0x16:
  case 0x5C:
    SetOrder((dwFlags & 0x20) ? 5 : 0x17);
    return;
  case 0x17:
  case 0x18:
  case 0xA2:
  case 0xAF:
  case 0xB0:
    return;
  case 0x53:
    return DispatchInteractWithGasMine();
  case 0x7D:
    return DispatchNukeLaunch();
  case 0x1D:
    return DispatchInfestEnd();
  case 0x96:
    return DispatchUndoFlyer();
  case 0x97:
    return DispatchUndoTransparentWorker();
  case 0x11:
    return DispatchPowerupCTF();
  case 0x61:
    return DispatchPowerup();
  }

  unsigned nOrder = bOrderId;
  BYTE bCount = bUnion85;
  bUnion85 = bCount - 1;
  if (bCount != 0)
    return;
  nOrder -= 5;
  bUnion85 = 8;
  if (nOrder > 0x9C)
    return;

  switch (nOrder) {
  case 0x97:
    return DispatchComputerAI_Bunker();
  case 0x9C:
    return DispatchRescuePassive();
  case 0x00:
    return DispatchGuardBunker();
  case 0x5B:
    return DispatchLoadUnit();
  }
}

// CUnit::ApproachMoveTarget_xy, oracle 0x438B80 (27 B)
BOOL CUnit::ApproachMoveTarget_xy(int nX, int nY)
{
  if (bUnionE3 != 0)
    return TRUE;
  return SetMoveTarget_xy(nX, nY);
}

// Moves toward the target unit, or ends the order once the unit is at rest
// out of sight of it and out of range.
BOOL CUnit::ApproachMoveTarget_CUnit(CUnit *pTarget)
{
  if (bUnionE3 != 0)
    return TRUE;
  if (!SpriteSeenBy(pTarget->pSprite, bPlayer) &&  // Hyperion: and slots past 7
      !IsWithinTargetRange(0x1C0, pTarget)) {
    if (AtMoveTarget() == 1) {
      EndDispatchOrder();
      return FALSE;
    }
    return TRUE;
  }
  return SetMoveTarget_CUnit(pTarget);
}

// Refreshes the unit's placebox thingy and selection circles after it moves.
// Each size is indexed through its own read of the type, which gives the
// oracle's two copies of it in EDX and ESI.
void CUnit::CircleUpdate()
{
  WORD wTypeY = wType;
  WORD wTypeX = wType;
  BOOL bMoved = reinterpret_cast<CThingy *>(this)->CalcVisibility(
      gUnitPlaceboxSize[wTypeX].x, gUnitPlaceboxSize[wTypeY].y);
  if (subunit && !(subunit->pSprite->flags.b & 0x20)) {
    subunit->pSprite->SetMask(pSprite->bVisibilityFlags);
    VisionHxSpriteSet(subunit->pSprite, VisionHxSprite(pSprite));  // Hyperion
  }
  if (!bMoved)
    return;
  if (pSprite->flags.b & 8)
    sub_4A7B80();
  pSprite->RemoveCircle();
  pSprite->RemoveTeamCircleIf();
  if (!(dwFlags & 2) && (wType < 0xCB || wType > 0xD5))
    return;
  if (IsDead())
    return;
  CThingy::AllocFromSprite(wType, pSprite);
}

// CUnit::DisruptorsUpdate
void CUnit::DisruptorsUpdate()
{
  for (CUnit *pUnit = GetUsedList(); pUnit != NULL; pUnit = pUnit->pNext) {
    DWORD dwFlags = pUnit->dwFlags;
    if ((dwFlags & 4u) == 0 || (dwFlags & 0x8000u) != 0) {
      pUnit->dwFlags = dwFlags & 0xFFFF7FFFu;
      if (pUnit->IsATransport()) {
        for (int i = 0; i < 8; i++) {
          CUnit *pCargo = pUnit->GetTransportUnit(i);
          if (pCargo != NULL)
            pCargo->dwFlags &= 0xFFFF7FFFu;
        }
      } else if (pUnit->subunit != NULL) {
        pUnit->subunit->dwFlags &= 0xFFFF7FFFu;
      }
    }
  }
  if (count_get_completion(0x69, 0x0B) == 0)
    return;
  for (CUnit *pUnit = GetPlayerUnitList(0x0B); pUnit != NULL;
       pUnit = pUnit->pPlayerUnitNext) {
    if (pUnit->wType == 0x69) {
      mrect_t  area;
      int nPosX = pUnit->pSprite->wPosX;
      upos_t nPosY = pUnit->pSprite->wPosY;
      area.top = PosS(nPosY - gUnitBoundBox[0x69].top);
      area.bottom = PosS(nPosY + gUnitBoundBox[0x69].bottom);
      area.left = PosS(nPosX - gUnitBoundBox[0x69].left);
      area.right = PosS(nPosX + gUnitBoundBox[0x69].right);
      sub_442B60(&area,
                 reinterpret_cast<CUnitTerranFindCb>(apply_disruption_web_status),
                 NULL);
    }
  }
}

// CUnit::IsBurning, oracle 0x438DD0 (69 B)
BOOL CUnit::IsBurning()
{
  int nMaxLife = (int)HandicapLife(gxUnitLife[wType], bPlayer) >> 8;  // Hyperion: at the owner's handicap
  if (nMaxLife == 0) {
    int nHp = (lHitPoints + 255) >> 8;
    nMaxLife = nHp != 0 ? nHp : 1;
  }
  int nPct = ((lHitPoints + 255) >> 8) * 100 / nMaxLife;
  return 33u >= (unsigned)nPct;
}

// CUnit::EnergyUpdate, oracle 0x438E20 (187 B)
void CUnit::EnergyUpdate()
{
  if ((gulUnitTypeFlags[wType] & 0x200000u) == 0)
    return;
  DWORD dwFlags = this->dwFlags;
  if ((dwFlags & 0x40000000u) != 0 || (dwFlags & 1u) == 0)
    return;
  if ((dwFlags & 0x300u) == 0 || (dwFlags & 0x800u) != 0) {
    WORD wEnergy = this->wEnergy;
    int nMax = GetMaxEnergyInProgress();
    if (wEnergy == nMax)
      return;
    this->wEnergy = static_cast<WORD>(wEnergy + 8);
    if (this->wEnergy > GetMaxEnergyInProgress())
      this->wEnergy = static_cast<WORD>(GetMaxEnergyInProgress());
  } else {
    if (NeedsMoreEnergy(GetCloakingCost())) {
      if (bBackgroundOrderId == 0x6D)
        SetBackgroundOrder(0x17);
      return;
    }
    SubtractEnergy(GetCloakingCost());
  }
  if ((pSprite->flags.b & 8) != 0)
    return pSprite->RedrawHP();
}

// Steps every unit list once per frame: order-sequence slots, dead units,
// movement, cloak timers, orders, bullets and the scanner list.
void CUnit::EntityUpdate()
{
  ai_reset_interceptor_call();
  if (sub_49C0F0())
    unit_gui_reset();
  if (--sgdwOrderSeqDelay_g == 0) {
    BYTE bSeq = 0;
    for (CUnit *pUnit = GetUsedList(); pUnit != NULL; pUnit = pUnit->pNext) {
      pUnit->bUnion85 = bSeq;
      bSeq = ++bSeq == 8 ? 0 : bSeq;
    }
    sgdwOrderSeqDelay_g = 0x96;
  }
  if (--sgdwBackgroundOrderSeqDelay_g == 0) {
    BYTE bSeq = 0;
    for (CUnit *pUnit = GetUsedList(); pUnit != NULL; pUnit = pUnit->pNext) {
      pUnit->bUnion91 = bSeq;
      bSeq = ++bSeq == 0x1E ? 0 : bSeq;
    }
    sgdwBackgroundOrderSeqDelay_g = 0x12C;
  }
  DisruptorsUpdate();
  CUnit *pNext;
  for (CUnit *pUnit = GetDeadList(); pUnit != NULL; pUnit = pNext) {
    pNext = pUnit->pNext;
    gpImageTargeter = pUnit;
    gpImageUnit = pUnit;
    if (pUnit->pSprite && (pUnit->pSprite->flags.b & 0x20)) {
      pUnit->pSprite->Kill();
      pUnit->pSprite = NULL;
    }
    reinterpret_cast<CThingy *>(pUnit)->SpriteUpdate();
    if ((!(pUnit->dwFlags & 0x4000) || sub_4B4A10(pUnit)) && pUnit->pSprite == NULL)
      pUnit->RemoveDead();
    CSprite *pSprite = pUnit->pSprite;
    if (pSprite) {
      if (byte_6663EC && gPlayerData[pUnit->bPlayer].nType == 2) {
        if (g_hxPlayers) {
          // Hyperion: a shared vision past slot 7 is stamped by MaskStampHx, where
          // 1.17's stamp refuses a mask past 0xFF whole (stage D)
          const PlayerSet q = PlayerVision(pUnit->bPlayer);
          const int bAir = (pUnit->dwFlags >> 2) & 1;
          if (q.Low8() != 0)
            sub_46BBB0(PosS(pSprite->wPosX), PosS(pSprite->wPosY), bAir,
                       static_cast<int>(q.Low8()), 1, 0);
          MaskStampHx(PosS(pSprite->wPosX), PosS(pSprite->wPosY), bAir, q, 1);
        } else {
          sub_46BBB0(PosS(pSprite->wPosX), PosS(pSprite->wPosY), (pUnit->dwFlags >> 2) & 1,
                     SharedVision[pUnit->bPlayer], 1, 0);
        }
      }
      pUnit->CircleUpdate();
    }
  }
  for (CUnit *pUnit = GetUsedList(); pUnit != NULL; pUnit = pUnit->pNext) {
    gpImageTargeter = pUnit;
    gpImageUnit = pUnit;
    pUnit->UpdateMovement();
    VisionHxCircleNote(pUnit, gUnitPlaceboxSize);  // Hyperion: the circle walk's
  }
  if (byte_6663EC) {
    for (CUnit *pUnit = GetScannerList(); pUnit != NULL; pUnit = pNext) {
      pNext = pUnit->pNext;
      gpImageTargeter = pUnit;
      gpImageUnit = pUnit;
      pUnit->sub_423260();
    }
  }
  // Hyperion: the walk's fog reads made beside it on the worker threads; the walk writes
  // no fog (playervision.h)
  VisionHxCirclesBegin();
  for (CUnit *pUnit = GetUsedList(); pUnit != NULL; pUnit = pUnit->pNext) {
    pUnit->CircleUpdate();
    if (pUnit->dwFlags & 0x300) {
      pUnit->bCloakCount = 0;
      if (pUnit->bUnion91) {
        pUnit->bUnion91--;
      } else {
        pUnit->sub_4325A0();
        pUnit->bUnion91 = 0x1E;
      }
    }
  }
  VisionHxCirclesEnd();  // Hyperion
  for (CUnit *pUnit = GetUsedList(); pUnit != NULL; pUnit = pNext) {
    pNext = pUnit->pNext;
    gpImageTargeter = pUnit;
    gpImageUnit = pUnit;
    pUnit->UpdatePrimary();
  }
  for (CUnit *pUnit = GetTransList(); pUnit != NULL; pUnit = pNext) {
    pNext = pUnit->pNext;
    if (pUnit->dwFlags & 0x300)
      pUnit->bCloakCount = 0;
    gpImageTargeter = pUnit;
    gpImageUnit = pUnit;
    pUnit->UpdateSecondary();
  }
  CloakUpdate();
  UpdateProtossPowerGrid();
  CBullet::NextBucket();
  for (CUnit *pUnit = GetScannerList(); pUnit != NULL; pUnit = pNext) {
    pNext = pUnit->pNext;
    gpImageTargeter = pUnit;
    gpImageUnit = pUnit;
    pUnit->UpdatePrimary();
  }
  gpImageTargeter = NULL;
  gpImageUnit = NULL;
}

// CUnit::SetDestination_xy, oracle 0x43A070 (46 B)
BOOL CUnit::SetDestination_xy(int nX, int nY)
{
  BOOL bResult = SetMoveTarget_xy(nX, nY);
  reinterpret_cast<CFlingy *>(this)->SetDestination(nX, nY);
  return bResult;
}

// CUnit::SetDestination_CUnit, oracle 0x43A0A0 (48 B)
BOOL CUnit::SetDestination_CUnit(CUnit *pTarget)
{
  BOOL bResult = SetMoveTarget_CUnit(pTarget);
  CSprite *pSprite = pTarget->pSprite;
  reinterpret_cast<CFlingy *>(this)->SetDestination(PosS(pSprite->wPosX),
                                                    PosS(pSprite->wPosY));
  return bResult;
}

// CUnit::SetMoveTarget_xy, oracle 0x43A0D0 (176 B)
// The point is built in the dead nX argument slot, which is the compiler colouring the
// slot and not a source assignment, then clipped and passed on to SetTarget_xy. The
// flag clear is dwFlags &= ~0x80000u, bit 19 alone; the listing's -80001h is the same
// value in two's complement. The top guard movsx-compares the move target against
// (nX, nY) and returns TRUE early, which is the signed reading of wMoveTargetX and Y.
BOOL CUnit::SetMoveTarget_xy(int nX, int nY)
{
  if (PosS(wMoveTargetX) == nX && PosS(wMoveTargetY) == nY)
    return TRUE;
  if (dwUnion134 != 0) {
    if (ComputerTransport(nX, nY) != 0)
      return FALSE;
  }
  sub_42C790();
  mpoint_t  pt;
  pt.x = PosS(nX);
  pt.y = PosS(nY);
  ClipToMap(&pt, wType);
  reinterpret_cast<CFlingy *>(this)->SetTarget_xy(pt.x, pt.y);
  dwFlags &= ~0x80000u;
  bUnionE3 = 0xF;
  if (pOrderQueueHead != 0 && order_unk7[pOrderQueueHead->bOrderId] == 0) {
    bMoveFlags |= 0x20;
    return TRUE;
  }
  bMoveFlags &= ~0x20;
  return TRUE;
}

// CUnit::SetMoveTarget_CUnit, oracle 0x43A180 (211 B)
// bArrived must live inside the outer if: declared after it, pTarget != pTracked reaches
// the arrived block, which is a different program of the same size.
BOOL CUnit::SetMoveTarget_CUnit(CUnit *pTarget)
{
  CFlingy *pFlingy = reinterpret_cast<CFlingy *>(this);
  CUnit *pTracked = pFlingy->pMoveTargetUnit;
  if (pTarget == pTracked) {
    CSprite *pSprite = pTracked->pSprite;
    BOOL bArrived =
        (pFlingy->wMoveTargetX == PosS(pSprite->wPosX) &&
         pFlingy->wMoveTargetY == PosS(pSprite->wPosY)) ||
        IsIntersecting(pTracked, PosPack(pFlingy->wMoveTargetX, pFlingy->wMoveTargetY));  // Hyperion: packed by value (stage E)
    if (bArrived) {
      dwFlags &= ~0x80000u;
      return TRUE;
    }
  }
  if (dwUnion134 != 0 && bOrderId != 0x60) {
    spos_t nTargetY = PosS(pTarget->pSprite->wPosY);
    spos_t nTargetX = PosS(pTarget->pSprite->wPosX);
    if (ComputerTransport(nTargetX, nTargetY) != 0)
      return FALSE;
  }
  sub_42C790();
  reinterpret_cast<CFlingy *>(this)->SetTarget_CUnit(pTarget);
  dwFlags &= ~0x80000u;
  bUnionE3 = 0xF;
  if (pOrderQueueHead != 0 && order_unk7[pOrderQueueHead->bOrderId] == 0) {
    bMoveFlags |= 0x20;
    return TRUE;
  }
  bMoveFlags &= ~0x20;
  return TRUE;
}

// CUnit::SetSubTarget, oracle 0x43A260 (9 B)
void CUnit::SetSubTarget(int nX, int nY)
{
  reinterpret_cast<CFlingy *>(this)->SetTargetTurningPoint(nX, nY);
}

// The collision update is nested under the unit-type test: both jumps go to
// the epilogue.
// The clip buffer is scoped to its own block and copied out to two shorts,
// which take its slots; with the buffer at function scope they spill to the
// parameter slots instead.
void CUnit::SetUnitPosition(upos_t nX, upos_t nY)
{
  spos_t sX, sY;
  {
    mpoint_t  buf;
    buf.x = PosS(nX);
    buf.y = PosS(nY);
    ClipToMap(&buf, wType);
    sX = buf.x;
    sY = buf.y;
  }
  int nSpriteX = PosS(pSprite->wPosX);
  int nSpriteY = PosS(pSprite->wPosY);
  reinterpret_cast<CFlingy *>(this)->SetXY(sX, sY);
  if (!IsDeadOrDying()) {
    int nMY = sY;
    int nMX = sX;
    SetMoveTarget_xy(nMX, nMY);
    reinterpret_cast<CFlingy *>(this)->SetDestination(nMX, nMY);
  }
  if ((gulUnitTypeFlags[wType] & 0x10) == 0) {
    CoordSort(sX - nSpriteX, sY - nSpriteY);
    if ((pSprite->flags.b & 0x20) == 0)
      UpdateUnitCollision();
  }
}

// The per-frame timers and regeneration: weapon and order cooldowns, shield
// regen, spell timers every eighth frame, the time-to-live and zerg burn damage.
void CUnit::StepUpdate()
{
  for (int i = 0; i < 3; i++) {
    if ((&bUnion54)[i])
      (&bUnion54)[i]--;
  }
  if (bUnion57)
    bUnion57--;
  if (gbUnitHasShields[wType]) {
    DWORD lMax = HandicapShields(guwUnitEnergy[wType], bPlayer) << 8;  // Hyperion: at the owner's handicap
    if (lShields != lMax) {
      int nNew = lShields + 7;
      lShields = nNew > (int)lMax ? lMax : nNew;
      if (pSprite->flags.b & 8)
        pSprite->RedrawHP();
    }
  }
  if ((wType == 0x25 || wType == 0x36) && !_unmapped_55[0])
    bUnion85 = 0;
  // The store in both arms is what schedules it between the test and the jump.
  if (dwFlags & 1) {
    _unmapped_106[1] = 0;
  } else {
    _unmapped_106[1] = 0;
    if (pSprite->flags.b & 0x20)
      goto skip;
  }
  if (++_unmapped_121[1] >= 8) {
    _unmapped_121[1] = 0;
    SpellUpdate();
  }
skip:
  if (!(dwFlags & 1))
    return;
  if ((gulUnitTypeFlags[wType] & 0x80) && lHitPoints > 0 &&
      lHitPoints != HandicapLife(gxUnitLife[wType], bPlayer))  // Hyperion: at the owner's handicap
    ModifyLife(4);
  EnergyUpdate();
  if (bUnionE3)
    bUnionE3--;
  if (wTimeToLive) {
    if (--wTimeToLive == 0) {
      Suicide();
      return;
    }
  }
  if (GetRace() == 1 && ((dwFlags & 2) || (gulUnitTypeFlags[wType] & 0x20)) &&
      IsBurning())
    DamageApply(0x14, NULL, bUnion90, 1);
}

// CUnit::Stop, oracle 0x43A490 (68 B)
void CUnit::Stop()
{
  if (sub_42AD10() != 0)
    return;
  reinterpret_cast<CFlingy *>(this)->StopMovement();
  // Hyperion: the point by field, where 1.17 compared DWORDs (stage E)
  mpoint_t pt = moveTarget;
  ClipToMap(&pt, wType);
  if (pt.x != moveTarget.x || pt.y != moveTarget.y) {
    moveTarget = pt;
    nextWaypoint = pt;
  }
  bUnionE3 = 0xF;
}

// Moves the unit one step, then carries its turret subunit along: turn,
// reposition on the overlay offset, and the walk-animation switch.
void CUnit::UpdateMovement()
{
  int nOldDir = bDirection;
  PathCalcMove();
  if (sgbUpdateLos_g)
    sub_423260();
  if (!(dwFlags & 1) || !subunit || (gulUnitTypeFlags[wType] & 0x10))
    return;
  int nTurn = bDirection - nOldDir;
  if (nTurn < 0)
    nTurn += 0x100;
  subunit->UpdateTurret(nTurn);
  shield_offset_t ptOff;
  pSprite->pMainImage->GetOverlayOffset(&ptOff, 2, 0);
  reinterpret_cast<CFlingy *>(subunit)->SetPosition256(
      reinterpret_cast<CFlingy *>(this)->lPositionX256,
      reinterpret_cast<CFlingy *>(this)->lPositionY256);
  subunit->pSprite->pMainImage->SetOffset((char)ptOff.x, (char)ptOff.y);
  gpImageUnit = subunit;
  // The moving bit is read afresh in each arm; a shared local merges the arms.
  if (!((bMoveFlags >> 1) & 1) && (subunit->dwFlags & 0x1000000)) {
    subunit->dwFlags &= ~0x1000000;
    if ((dwFlags & 0x20000) && !(subunit->bMoveFlags & 8))
      subunit->pSprite->SetAnimation(0x0C, 1);
  } else if (((bMoveFlags >> 1) & 1) && !(subunit->dwFlags & 0x1000000)) {
    subunit->dwFlags |= 0x1000000;
    if (!(subunit->bMoveFlags & 8))
      subunit->pSprite->SetAnimation(0x0B, 1);
  }
  subunit->UpdateMovement();
  gpImageUnit = this;
}

// CUnit::UpdatePrimary, oracle 0x43A720 (78 B)
void CUnit::UpdatePrimary()
{
  UpdateFlash();
  StepUpdate();
  RunOrders();
  RunBGOrders();
  if (subunit != 0 && (gulUnitTypeFlags[wType] & 0x10) == 0) {
    gpImageUnit = subunit;
    subunit->UpdatePrimary();
    gpImageUnit = this;
  }
  reinterpret_cast<CThingy *>(this)->SpriteUpdate();
}

// CUnit::UpdateSecondary, oracle 0x43A770 (130 B)
void CUnit::UpdateSecondary()
{
  if (subunit != NULL && (gulUnitTypeFlags[wType] & 0x10u) == 0) {
    gpImageUnit = subunit;
    subunit->UpdateSecondary();
    gpImageUnit = this;
  }
  PathCalcMove();
  StepUpdate();
  RunTransportOrders();
  switch (bBackgroundOrderId) {
    case 0x3F:
      DispatchTrainFighter();
      break;
    case 0x6D:
      DispatchCloak();
      return reinterpret_cast<CThingy *>(this)->SpriteUpdate();
    case 0x6E:
      DispatchDecloak();
      return reinterpret_cast<CThingy *>(this)->SpriteUpdate();
  }
  return reinterpret_cast<CThingy *>(this)->SpriteUpdate();
}

// CUnit::UpdateTurret, oracle 0x43A800 (118 B)
void CUnit::UpdateTurret(int nDirection)
{
  if (pOrderTarget != NULL) {
    dwFlags &= 0xFDFFFFFFu;
  } else {
    if (bUnion21 == subunit->bUnion21)
      dwFlags |= 0x2000000u;
  }
  if ((dwFlags & 0x2000000u) != 0) {
    reinterpret_cast<CFlingy *>(this)->FaceDirection(subunit->bUnion21);
  } else {
    bUnion21 = bDirection = static_cast<BYTE>(static_cast<BYTE>(nDirection) + bDirection);
  }
  if (wType == 4 || wType == 0x12)
    AlignWithTurretBase();
}

// int apply_disruption_web_status, oracle 0x43A880 (90 B)
// Returns 0 on every path (both exits materialise xor eax,eax); sub_442B60
// tests the callback's EAX.
int apply_disruption_web_status(CUnit *pUnit)
{
  DWORD dwFlags = pUnit->dwFlags;
  if ((dwFlags & 4u) == 0) {
    pUnit->dwFlags = dwFlags | 0x8000u;
    if (pUnit->IsATransport()) {
      for (int i = 0; i < 8; i++) {
        CUnit *pCargo = pUnit->GetTransportUnit(i);
        if (pCargo != NULL)
          pCargo->dwFlags |= 0x8000u;
      }
    } else {
      CUnit *pSub = pUnit->subunit;
      if (pSub != NULL)
        pSub->dwFlags |= 0x8000u;
    }
  }
  return 0;
}

// CUnit::DispatchBurrowed, oracle 0x43C810 (106 B)
void CUnit::DispatchBurrowed()
{
  if (wType == 0x67)
    return EndUninterruptableDispatchOrder();

  if (wType == 0x26) {
    COrder *pOrder = pOrderQueueHead;
    if (pOrder != NULL && pOrder->bOrderId == 0x2A) {
      SetOrder(0x76);
      AppendOrder(0x2A, pOrderQueueHead);
      return EndUninterruptableDispatchOrder();
    }
  }

  if (dwUnion134 != 0 && bUnion54 == 0) {
    bUnion54 = 7;
    int nSpells = ai_update_unit_spells(this, 0);
    if (nSpells == 0)
      AIPickAttackTarget(nSpells, 1, nSpells);
  }
}

// CUnit::DispatchDeburrow, oracle 0x43C880 (212 B)
void CUnit::DispatchDeburrow()
{
  switch (bOrderState) {
  case 0:
    pSprite->bElevationLevel = byte_520A98[wType];
    dwFlags &= ~0x200000u;
    UpdateUnitCollision();
    pSprite->flags.b &= 0xBF;
    SetBackgroundOrder(0x17);
    pSprite->SetAnimation(0x1A, 1);
    gamesnd_play(0x11, this, 1, 0);
    bOrderState = 1;
    return;
  case 1:
    dwUnionE4 = 0x80000000;
    _unmapped_90[1] = 0;
    bOrderState = 2;
    // fall through
  case 2:
    if (bOrderFlags & 4) {
      bOrderFlags &= ~4u;
      MakeUnburrowed();
      SyncAllStatusEffects();
      pSprite->ShowPowerup();
      pSprite->SetFacing(bUnion21);
      bOrderState = 3;
    }
    return;
  case 3:
    if (pUnion74 == NULL)
      SetOrder_interrupt(gubUnitOrderGuard[wType], 0);
    EndUninterruptableDispatchOrder();
    status_set_update();
    return;
  }
}

// ai_reset_interceptor_call, oracle 0x4B0BD0 (11 B)
__declspec(noinline) void ai_reset_interceptor_call()
{
  dword_6B6900 = 0;
}

// sub_49C0F0, oracle 0x49C0F0 (70 B)
BOOL sub_49C0F0()
{
  for (int i = 0; i < SelectionMax(); ++i) {
    CUnit *pUnit = NewUnits()[i];
    if (pUnit == NULL)
      return TRUE;
    if (pUnit->dwFlags & 0x400)
      continue;
    if (pUnit->bLockdownTimer != 0)
      continue;
    if (pUnit->bStasisTimer != 0)
      continue;
    if (pUnit->bParalyzeTimer == 0)
      return FALSE;
  }
  return TRUE;
}

// sub_4B4A10, oracle 0x4B4A10 (33 B). Takes the dead unit in ECX from
// EntityUpdate and passes it straight through to the tail jump.
__declspec(noinline) BOOL sub_4B4A10(CUnit *pUnit)
{
  DWORD dwTimer = dword_6C46D8;
  if (dwTimer != 0) {
    dword_6C46D8 = dwTimer - 1;
    return FALSE;
  }
  dword_6C46D8 = 3;
  return sub_4B47F0(pUnit);
}

// CSprite::UpdateFlash, oracle 0x43A540 (61 B)
// Counts the flash down and shows or hides the selection circle.
void CSprite::UpdateFlash(BYTE bColor)
{
  if (bFlashCounter == 0)
    return;
  bFlashCounter--;
  if ((bVisibilityFlags & gbLocalMask) == 0)
    bFlashCounter = 0;
  BYTE b = bFlashCounter;
  if ((b & 8) || (b == 0 && (flags.b & 8)))
    AllocSelect(bColor);
  else
    RemoveCircle();
}

// Ticks the unit's selection-circle flash, with the owner's circle colour.
void CUnit::UpdateFlash()
{
  if (gulUnitTypeFlags[wType] & 0x10)
    return;
  if (pSprite->flags.b & 0x20)
    return;
  pSprite->UpdateFlash(gbSelCircleColor_g[bPlayer]);
}

// Stops the unit where it stands and re-aims its flingy at the clipped spot.
void CUnit::StopUnitMovement()
{
  Stop();
  reinterpret_cast<CFlingy *>(this)->SetDestination(PosS(wMoveTargetX), PosS(wMoveTargetY));
}