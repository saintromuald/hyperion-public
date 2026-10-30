// CUnitZBuild.cpp -- zerg drone build and structure placement. Bodies are in oracle VA order.
//
// A callee defined in this file can be spliced into a caller here by /Ob2, while the
// oracle, built with WPO off, cannot splice across a TU boundary: a callee left in another
// TU reproduces that for free, and __declspec(noinline) is the lever when one must live
// here.

#include "oc_data.h"  // Hyperion: OC_VA
#include "../h/playertables.h"  // Hyperion: player slots past 11
#include <string.h>                 // memcpy -- the selection staging copy

#include "../h/CUnitZBuild.h"
#include "../h/CUnitBuild.h"
#include "../h/CUnitTSpells.h"
#include "../h/CUnitZSpells.h"
#include "../h/CSprite.h"
#include "../h/CThingy.h"
#include "../h/CImage.h"
#include "../h/point16.h"
#include "../h/TrigAct.h"
#include "../h/collide.h"
#include "../h/SAI_Banker.h"
#include "../h/creep.h"
#include "../h/minimap.h"
#include "../h/gamesnd.h"
#include "../h/CFlingy.h"
#include "../h/banker.h"
#include "../h/placebox.h"
#include "../h/SAI_Build.h"
#include "../h/rect32.h"
#include "../h/CUnit.h"
#include "../h/tile2.h"        // sub_4B46E0, sub_4B4DD0, tile_is_occupied, tile_can_have_creep
#include "../gen/globals117.h"
#include "../port/render/port_view.h"  // Hyperion: g_portView
#include "../h/selectlimits.h"  // Hyperion: SELECTION_MAX
#include "../h/handicap.h"  // Hyperion: a player's handicap

// The globals this TU reaches, dual-spelled: one #ifdef per name, never one over the whole
// block, since the generated set grows over time and a single guard would go false and
// undeclare every name under it.

// gpNewUnits -- CUnit *[12] @0x6A6070 (select.cpp). The staging array a band
// select builds before it is committed.
#ifdef OC_DLL_MODE
  #ifndef gpNewUnits
  #define gpNewUnits (reinterpret_cast<CUnit **>(OC_VA(0x6A6070u)))
  #endif
#else
extern CUnit *gpNewUnits[SELECTION_MAX] OC_UNALIGNED;
#endif

// glCheatBits -- DWORD @0x5432FC (cheat.cpp). Registered.
#ifdef OC_DLL_MODE
  #ifndef glCheatBits
  #define glCheatBits (*reinterpret_cast<DWORD *>(OC_VA(0x5432FCu)))
  #endif
#else
extern DWORD glCheatBits;
#endif

// glGameCounts_g -- DWORD[28][12] @0x5290C4 (TrigEnum.cpp). Registered; rows 5
// and 2 are the absolutes 0x5291B4 and 0x529124, each 48 B apart.
#ifdef OC_DLL_MODE
  #ifndef glGameCounts_g
  #define glGameCounts_g (reinterpret_cast<DWORD (*)[12]>(OC_VA(0x5290C4u)))
  #endif
#else
extern PlayerGameCountsGrid glGameCounts_g;
#endif

// guwUnitScoreMake -- WORD[228] @0x5240C8 (arr/units.cpp). Sheet name
// data_naming_1.17.csv; element 0x29 is the absolute 0x52411A.
#ifdef OC_DLL_MODE
  #ifndef guwUnitScoreMake
  #define guwUnitScoreMake (reinterpret_cast<WORD *>(OC_VA(0x5240C8u)))
  #endif
#else
extern WORD guwUnitScoreMake[228] OC_UNALIGNED;
#endif

// guiUnitBuildImage -- DWORD[228] @0x51FFE0 (arr/units.cpp). Registered, but
// no landed row uses it yet, so gen does not emit it.
#ifdef OC_DLL_MODE
  #ifndef guiUnitBuildImage
  #define guiUnitBuildImage (reinterpret_cast<DWORD *>(OC_VA(0x51FFE0u)))
  #endif
#else
extern DWORD guiUnitBuildImage[228] OC_UNALIGNED;
#endif

// guwUnitTimeToBuild -- WORD[228] @0x5238B0 (arr/units.cpp). Sheet name
// data_naming_1.17.csv; stride 2 and a 16-bit load off an unbiased type id.
#ifdef OC_DLL_MODE
  #ifndef guwUnitTimeToBuild
  #define guwUnitTimeToBuild (reinterpret_cast<WORD *>(OC_VA(0x5238B0u)))
  #endif
#else
extern WORD guwUnitTimeToBuild[228] OC_UNALIGNED;
#endif

// gubUnitMoveClass -- BYTE[228] @0x520B80 (arr/units.cpp). Sheet name
// data_naming_1.17.csv; scale-free byte load off an unbiased wType.
#ifdef OC_DLL_MODE
  #ifndef gubUnitMoveClass
  #define gubUnitMoveClass (reinterpret_cast<BYTE *>(OC_VA(0x520B80u)))
  #endif
#else
extern BYTE gubUnitMoveClass[228] OC_UNALIGNED;
#endif

// gUnitPlaceboxSize -- point16_t[228] @0x522728 (arr/units.cpp). Registered.
#ifdef OC_DLL_MODE
  #ifndef gUnitPlaceboxSize
  #define gUnitPlaceboxSize (reinterpret_cast<point16_t *>(OC_VA(0x522728u)))
  #endif
#else
extern point16_t gUnitPlaceboxSize[228] OC_UNALIGNED;
#endif

// g_ActiveHumanID -- DWORD @0x508D48 (net_glue.cpp). Registered.
#ifdef OC_DLL_MODE
  #ifndef g_ActiveHumanID
  #define g_ActiveHumanID (*reinterpret_cast<DWORD *>(OC_VA(0x508D48u)))
  #endif
#else
extern DWORD g_ActiveHumanID;
#endif

// g_LocalHumanID -- DWORD @0x508D54 (net_glue.cpp). Registered.
#ifdef OC_DLL_MODE
  #ifndef g_LocalHumanID
  #define g_LocalHumanID (*reinterpret_cast<DWORD *>(OC_VA(0x508D54u)))
  #endif
#else
extern DWORD g_LocalHumanID;
#endif

// The hit points a building had before it started morphing, CUnit +0xAE as a
// WORD in whole HP. CUnit.h spells the slot `_unmapped_AE[2]`.
#define UNIT_PREV_HP(u) (*reinterpret_cast<WORD *>(reinterpret_cast<BYTE *>(u) + offsetof(CUnit, wPrevHP)))

// The type this unit hatched from, CUnit +0x88 as a WORD. CUnit.h still spells
// the range `_unmapped_86[9]`, so it is reached by offset.
#define PREV_UNIT_TYPE(u) (*reinterpret_cast<WORD *>(reinterpret_cast<BYTE *>(u) + offsetof(CUnit, wPrevUnitType)))

// A dropped powerup's pick-up position, CUnit +0xD0/+0xD2 as two WORDs.
// Hyperion: by name, positions of 4 bytes on x64 (stage E)
#define POWERUP_ORIGIN_X(u) (PosU((u)->wPowerupOriginX))
#define POWERUP_ORIGIN_Y(u) (PosU((u)->wPowerupOriginY))


// sub_4B46E0, tile_is_occupied and tile_can_have_creep come from tile2.h, which
// seats all three; the creep walk below is their only caller here.

// select_units -- oracle 0x49CF80 (CUnitInit.cpp). Commits a resolved unit list
// as the local player's selection. Same spelling as src117/lang/usel.cpp.
void __fastcall select_units(int nCount, CUnit **ppUnits, BYTE a3, int a4);

// The bodies, in oracle VA order.

// Cancels a zerg building's morph. An upgrading building goes back through
// sub_43B500; anything else refunds, drops a cancel thingy and reverts to a
// drone, or -- for an extractor -- spawns the drone as a new unit and dies.
void sub_43B380(intptr_t nArg0)
{
  CUnit *pUnit = reinterpret_cast<CUnit *>(nArg0);
  CSprite *pSprite;
  CUnit *pDrone;
  CThingy *pThingy;
  upos_t wX;
  upos_t wY;
  spos_t sX;
  int nLife;
  int nOldType;

  if (pUnit->IsZergBldgUpgrading())
  {
    pUnit->sub_43B500();
    return;
  }

  nLife = UNIT_PREV_HP(pUnit) << 8;
  refund_unit_partial(pUnit->bPlayer, pUnit->wType);
  glGameCounts_g[5][pUnit->bPlayer] -= guwUnitScoreMake[0x29];
  --glGameCounts_g[2][pUnit->bPlayer];

  if (pUnit->wType == 0x95)
  {
    pSprite = pUnit->pSprite;
    wX = pSprite->wPosX;
    wY = PosU(pSprite->wPosY);
    sX = PosS(wX);
    pDrone = CreateUnitAt(0x29, sX, PosS(wY), pUnit->bPlayer);
    if (pDrone == 0)
    {
      CUnit::ShowCreateError(pUnit->bPlayer);
      pUnit->Suicide();
      return;
    }

    pDrone->FinishedMaking();
    pDrone->SetLife(nLife);
    pUnit->Suicide();
    return;
  }

  pSprite = pUnit->pSprite;
  wX = pSprite->wPosX;
  wY = PosU(pSprite->wPosY);
  sX = PosS(wX);
  pThingy = CThingy::AllocThingy(0xB6, sX, PosS(wY), 0);
  if (pThingy != 0)
  {
    pThingy->pSprite->bElevationLevel =
        static_cast<BYTE>(pUnit->pSprite->bElevationLevel + 1);
    pThingy->UpdateVisibility();
  }

  nOldType = pUnit->wType;
  gamesnd_play(4, pUnit, 1, 0);
  pUnit->TransformType(0x29);
  town_rejoin_building(pUnit);
  pUnit->FinishedMaking();
  pUnit->pSprite->SetShadowOffset(7);
  pUnit->pSprite->InvalidateDrawInfo();
  pUnit->SetOrder(0x96);
  pUnit->SetOrder_interrupt(gubUnitOrderGuard[pUnit->wType], 0);
  pUnit->SetLife(nLife);
  pSprite = pUnit->pSprite;
  sub_4B46E0(nOldType, PosS(pSprite->wPosX),
             PosS(pSprite->wPosY), 0);
}

// Cancels a building's morph: refunds the queued type, restores the pre-morph
// hit points and plays the cancel sound.
void CUnit::sub_43B500()
{
  LONG lLife;

  refund_unit_partial(this->bPlayer, this->wBuildQueue[this->bBuildQueueSlot]);
  this->wBuildQueue[this->bBuildQueueSlot] = 0xE4;
  lLife = this->lHitPoints;
  this->dwFlags |= 1;
  count_remove_completion(this, 1);
  this->dwFlags &= ~1;
  FinishedMaking();
  SetLife(lLife);
  gamesnd_play(4, this, 1, 0);
  this->pSprite->SetAnimation(0x0F, 1);
}

// Clamps a rectangle to the 640x400 game screen, in place. TRUE means the
// rectangle is entirely off screen and nothing was clamped. The seated
// declaration takes void *, so the cast is here.
BOOL ClipToGameScreen(void *pArg0)
{
  rect32_t *pRect = (rect32_t *)pArg0;
  LONG lRight = pRect->right;
  LONG lBottom;

  if (lRight <= 0)
    return TRUE;

  lBottom = pRect->bottom;
  if (lBottom <= 0)
    return TRUE;
  // Hyperion: the map as drawn, the view's width by its map rows,
  // 640 by 400 in 1.17.
  if (pRect->left >= g_portView.nWidth)
    return TRUE;
  if (pRect->top >= g_portView.nMapRows)
    return TRUE;

  if (pRect->left < 0)
    pRect->left = 0;
  if (lRight >= g_portView.nWidth)
    pRect->right = g_portView.nWidth;
  if (pRect->top < 0)
    pRect->top = 0;
  if (lBottom >= g_portView.nMapRows)
    pRect->bottom = g_portView.nMapRows;

  return FALSE;
}

// Splits a morphing unit in two: this one takes the first spawn offset, a
// freshly created copy takes the second and inherits the parent's state.
// The sprite position is read inside the first expression that uses it; a
// plain assignment statement ahead of the block moves the wType load with it.
// __restrict on pSprite lets the wPosY load move above the pt0.x store.
intptr_t CUnit::BirthCopy()
{
  shield_offset_t off0;
  shield_offset_t off1;
  mpoint_t  pt0;
  mpoint_t  pt1;
  CSprite *__restrict pSprite;
  CUnit *pNew;
  BYTE bDirection;
  int nType;
  WORD wSpriteX;
  WORD wSpriteY;

  this->pSprite->GetTwinSpawnOffsets(&off0, &off1);

  pSprite = this->pSprite;
  pt0.x = PosS(off0.x + (wSpriteX = pSprite->wPosX));
  pt0.y = PosS(off0.y + (wSpriteY = PosU(pSprite->wPosY)));
  pt1.x = PosS(off1.x + wSpriteX);
  pt1.y = PosS(off1.y + wSpriteY);

  nType = this->wType;
  ClipToMap(&pt0, static_cast<WORD>(nType));
  ClipToMap(&pt1, static_cast<WORD>(nType));
  SetUnitPosition(pt0.x, pt0.y);

  nType = this->wType;
  pNew = CreateUnitAt(nType, pt1.x, pt1.y, this->bPlayer);
  if (pNew == 0)
  {
    CUnit::ShowCreateError(this->bPlayer);
    return 0;
  }

  bDirection = 0;
  switch (this->wType)
  {
  case 0x25:
    bDirection = 0x12;
    break;
  case 0x2F:
    bDirection = 2;
    break;
  }

  reinterpret_cast<CFlingy *>(pNew)->FaceDirection(bDirection * 8);
  pNew->FinishedMaking();
  pNew->CopyStatusEffects(this);

  if (g_ActiveHumanID == g_LocalHumanID && (this->pSprite->flags.b & 8))
    pNew->SelectBirthedCopy();

  return reinterpret_cast<intptr_t>(pNew);
}

// The build-self state machine a zerg building runs while it grows. States 0..5
// walk the timer and the animations; state 6 completes, retypes a morphed
// structure and lays the creep. nType is the queued type when that type is a
// morph target, otherwise the unit's own.
void CUnit::DispatchBuildSelfZerg()
{
  CSprite *pSprite;
  CImage *pImage;
  BYTE bOrderFlags;
  int nType;
  int nLife;
  int nNewLife;
  int nGain;

  nType = this->wBuildQueue[this->bBuildQueueSlot];
  if (!type_is_morphed_structure(nType))
    nType = this->wType;

  switch (this->bOrderState)
  {
  case 0:
    if (this->wRemainingBuildTime < guwUnitTimeToBuild[nType] * 3U / 4)
      this->bOrderState = 1;
    break;

  case 1:
    if (this->wType == 0x95)
    {
      pImage = this->pSprite->OverlayGetImage(0x159, 0x159);
      if (pImage != 0)
        pImage->Kill();
    }
    this->pSprite->SetAnimation(0x0D, 1);
    this->bOrderState = 2;
    break;

  case 2:
    if (this->wRemainingBuildTime < static_cast<WORD>(guwUnitTimeToBuild[nType] / 2))
      this->bOrderState = 3;
    break;

  case 3:
    this->pSprite->SetAnimation(0x0E, 1);
    this->bOrderState = 4;
    break;

  case 4:
    if (this->wRemainingBuildTime == 0)
      this->bOrderState = 5;
    break;

  case 5:
    this->pSprite->SetAnimation(0x0F, 1);
    this->bOrderState = 6;
    gamesnd_build_complete(this);
    break;

  case 6:
    bOrderFlags = this->bOrderFlags;
    if ((bOrderFlags & 4) == 0)
      return;
    this->bOrderFlags = static_cast<BYTE>(bOrderFlags & ~4);

    if (type_is_morphed_structure(nType))
    {
      this->dwFlags |= 1;
      nLife = this->lHitPoints;
      count_remove_completion(this, 0);
      this->dwFlags &= ~1;
      TransformType(nType);
      // Hyperion: both maximums at the owner's handicap
      nNewLife = HandicapLife(gxUnitLife[nType], this->bPlayer) -
                 HandicapLife(gxUnitLife[PREV_UNIT_TYPE(this)], this->bPlayer) + nLife;
      if (nNewLife < 0x100)
        nNewLife = 0x100;
      SetLife(nNewLife);
      this->wRemainingBuildTime = 0;
    }

    FinishedMaking();
    sub_4B4DD0(this);
    this->pSprite->SetAnimation(0x0F, 1);
    pSprite = this->pSprite;
    CUnit::ZergPlaceBldg(nType, PosS(pSprite->wPosX),
                         PosS(pSprite->wPosY));
    return;
  }

  CountDownToCompletion();

  if (!type_is_morphed_structure(nType))
  {
    nGain = this->wHPGain;
    if (glCheatBits & 2)
      nGain <<= 4;
    ModifyLife(static_cast<WORD>(nGain));
  }
}

// The drone has arrived at the build site. Re-checks placement and cost, pays,
// then hands off to the extractor or the ground-structure path.
// posY is read between the two box reads; that order homes it at [ebp-4h] and
// keeps posX in a register.
void CUnit::DispatchDroneBuild()
{
  CSprite *pSprite = this->pSprite;
  upos_t wPosX;
  upos_t wPosY;
  WORD wBoxX;
  WORD wBoxY;
  int nType;
  CUnit *pPowerup;

  if (pSprite->wPosX == PosU(this->wOrderTargetX)
      && PosU(pSprite->wPosY) == PosU(this->wOrderTargetY))
  {
    nType = this->wBuildQueue[this->bBuildQueueSlot];
    wPosX = pSprite->wPosX;
    wBoxX = static_cast<WORD>(gUnitPlaceboxSize[static_cast<WORD>(nType)].x);
    wPosY = pSprite->wPosY;
    wBoxY = static_cast<WORD>(gUnitPlaceboxSize[static_cast<WORD>(nType)].y);
    if (mtx_query_place_bldg(this, this->bPlayer,
                             (PosS(wPosX) - static_cast<short>(wBoxX) / 2) / 32,
                             (PosS(wPosY) - static_cast<short>(wBoxY) / 2) / 32,
                             nType, 0, 0, 1, 1) != 0
        || this->pSprite->wPosX != PosU(this->wOrderTargetX)
        || PosU(this->pSprite->wPosY) != PosU(this->wOrderTargetY))
    {
      gamesnd_build_error(this, 0x358);
    }
    else if (bldg_get_unit_cost(this->bPlayer, static_cast<WORD>(nType), 1, 1))
    {
      bldg_deduct_resources(this->bPlayer);
      pPowerup = reinterpret_cast<CUnit *>(GetHeldPowerup());
      if (pPowerup != 0)
        DropInventory();

      if (static_cast<WORD>(nType) == 0x95)
        DroneBuildExtractor();
      else
        DroneBuildStructure(nType);

      if (pPowerup != 0)
        pPowerup->SetUnitPosition(POWERUP_ORIGIN_X(pPowerup), POWERUP_ORIGIN_Y(pPowerup));
      return;
    }

    this->pSprite->SetShadowOffset(7);
    this->pSprite->InvalidateDrawInfo();
    SetNextOrder(0x96);
    EndUninterruptableDispatchOrder();
    return;
  }

  pSprite->SetShadowOffset(7);
  this->pSprite->InvalidateDrawInfo();
  SetNextOrder(0x96);
  EndUninterruptableDispatchOrder();
}

// Puts a drone on the two-step build order: face the site, then start.
void CUnit::DispatchDroneStartBuild()
{
  SetOrder_xy(0x46, this->wOrderTargetX, this->wOrderTargetY);
  SetOrder_xy_interrupt(0x1A, this->wOrderTargetX, this->wOrderTargetY, 0);
}

// The morph state machine a zerg building runs while it changes type. State 0
// pays and retypes, state 1 counts the build timer down, state 2 completes.
void CUnit::DispatchMorph()
{
  BYTE bOrderFlags;
  int nOldType;

  switch (this->bOrderState)
  {
  case 0:
    if (this->wType != 0x23 && this->wType != 0x26 && this->wType != 0x2B)
      return;

    if (!bldg_check_food(this->bPlayer, this->wBuildQueue[this->bBuildQueueSlot], 1))
    {
      RemoveAllBuildQueue();
      SetOrder(gubUnitOrderGuard[this->wType]);
      return;
    }

    count_remove_completion(this, 0);
    this->dwFlags &= ~1;
    if (this->wType == 0x23)
      TransformType(0x24);
    else if (this->wType == 0x26)
      TransformType(0x61);
    else if (this->wType == 0x2B)
      TransformType(0x3B);
    else
      return;

    SetButtonset(this->wType);
    this->wRemainingBuildTime =
        guwUnitTimeToBuild[this->wBuildQueue[this->bBuildQueueSlot]];
    this->bOrderState = 1;
    break;

  case 1:
    if (CountDownToCompletion())
    {
      this->pSprite->SetAnimation(0x0D, 1);
      this->bOrderState = 2;
    }
    break;

  case 2:
    bOrderFlags = this->bOrderFlags;
    if ((bOrderFlags & 4) == 0)
      return;
    this->bOrderFlags = static_cast<BYTE>(bOrderFlags & ~4);

    nOldType = this->wType;
    TransformType(this->wBuildQueue[this->bBuildQueueSlot]);
    if (this->wType == 0x29 || this->wType == 0x2A)
      register_unit_for_ai(this, this);
    gamesnd_create_man(this);
    this->wBuildQueue[this->bBuildQueueSlot] = 0xE4;

    if (guiUnitBuildImage[this->wType] != 0)
    {
      SyncConstructionGraphic(1);
      this->pSprite->SetAnimation(0x0D, 1);
      SetOrder(0x29);
    }
    else
    {
      FinishedMaking();
      if (nOldType == 0x24)
        RallyToFactoryTarget(this->pUnion80);
    }
    break;
  }
}

// Places a nydus canal exit at the order target and links it back to the
// entrance. Every arm leaves through EndDispatchOrder. The player byte is read
// into a local: without it MSVC saves one callee-saved register instead of two.
void CUnit::DispatchMorphCanal()
{
  CUnit *pExit;
  BYTE bPlayer;

  bPlayer = this->bPlayer;
  if (mtx_query_place_bldg(this, bPlayer,
                           (this->wOrderTargetX - gUnitPlaceboxSize[0x86].x / 2) / 32,
                           (this->wOrderTargetY - gUnitPlaceboxSize[0x86].y / 2) / 32,
                           0x86, 0, 0, 1, 0))
  {
    gamesnd_build_error(this, 0x358);
    EndDispatchOrder();
    return;
  }

  pExit = CreateUnitAt(0x86, this->wOrderTargetX, this->wOrderTargetY, this->bPlayer);
  if (pExit == 0)
  {
    CUnit::ShowCreateError(this->bPlayer);
    EndDispatchOrder();
    return;
  }

  pExit->SyncConstructionGraphic(1);
  pExit->lHitPoints = static_cast<DWORD>(HandicapLife(gxUnitLife[pExit->wType], pExit->bPlayer)) / 10;  // Hyperion: at the owner's handicap
  pExit->SetOrder(0x2D);
  pExit->SetButtonset(0xE4);
  sub_4B4DD0(pExit);
  this->pUnionD0 = pExit;
  pExit->pUnionD0 = this;
  EndDispatchOrder();
}

// A unit finishes hatching: registers it with the AI, drops the construction
// graphic, nudges the sprite up for the birth animation and passes the rally
// order to the twin as well. gulUnitTypeFlags bit 10 is what makes a twin.
void CUnit::DispatchZergBirth()
{
  mpoint_t  pt;
  upos_t wOrigX;
  upos_t wOrigY;
  CUnit *pTwin;
  DWORD dwTwin;
  BYTE bOrderFlags;
  BYTE bNudge;
  int nOffset;

  bNudge = 1;
  bOrderFlags = this->bOrderFlags;
  if ((bOrderFlags & 4) == 0)
    return;
  this->bOrderFlags = static_cast<BYTE>(bOrderFlags & ~4);

  dwTwin = gulUnitTypeFlags[this->wType] & 0x400;
  if (dwTwin != 0 || PREV_UNIT_TYPE(this) == 0x3B)
    bNudge = 0;

  pTwin = 0;
  if (dwTwin != 0)
  {
    pTwin = reinterpret_cast<CUnit *>(BirthCopy());
    if (pTwin != 0)
      register_unit_for_ai(this, pTwin);
  }

  if (this->wType != 0x29 && this->wType != 0x2A)
    register_unit_for_ai(this, this);

  SyncConstructionGraphic(0);
  FinishedMaking();

  wOrigX = this->pSprite->wPosX;
  wOrigY = PosU(this->pSprite->wPosY);
  pt.x = PosS(wOrigX);
  pt.y = PosS(wOrigY);

  if (bNudge != 0)
  {
    nOffset = 0;
    if (gubUnitMoveClass[this->wType] == 0xC1)
      nOffset = -7;
    else if (this->dwFlags & 4)
      nOffset = -0x2A;
    pt.y = PosS(nOffset + wOrigY);
  }

  ClipToMap(&pt, this->wType);
  if (pt.x != PosS(wOrigX) || pt.y != PosS(wOrigY))
    SetUnitPosition(pt.x, pt.y);

  if (PREV_UNIT_TYPE(this) != 0x3B && PREV_UNIT_TYPE(this) != 0x61)
  {
    RallyToFactoryTarget(this->pUnion80);
    if (pTwin != 0)
      pTwin->RallyToFactoryTarget(this->pUnion80);
  }

  EndUninterruptableDispatchOrder();
}

// Turns the drone into the building it is morphing into: buttonset, graphic,
// one tenth of the finished hit points, and the build order.
void CUnit::DroneBuild()
{
  sub_4B4DD0(this);
  if (IsRallyer())
    SetButtonset(0xEA);
  else
    SetButtonset(0xE9);
  SyncConstructionGraphic(1);
  this->lHitPoints = (DWORD)HandicapLife(gxUnitLife[this->wType], this->bPlayer) / 10;  // Hyperion: at the owner's handicap
  SetOrder(0x2D);
  this->wBuildQueue[this->bBuildQueueSlot] = 0xE4;
}

// Morph onto a vespene geyser. The geyser is replaced in place when it is
// free; otherwise the drone hands off to the unit already there and dies.
void CUnit::DroneBuildExtractor()
{
  CUnit *pOther = ReplaceGeyser(0x95);

  if (pOther == 0)
  {
    this->pSprite->SetShadowOffset(7);
    this->pSprite->sub_416970();
    reinterpret_cast<CFlingy *>(this)->SetDestination(
        PosS(this->pSprite->wPosX), this->pSprite->wPosY - 7);
    SetNextOrder(0x96);
    EndUninterruptableDispatchOrder();
    return;
  }

  town_move_morphed_building(this, pOther);
  SelfDestruct();
  pOther->DroneBuild();
  pOther->pSprite->AllocImageBelow(0x159, 0, 0, 0);
}

// Morph into a ground structure: clear the completion flag, retype, then the
// shared drone-build tail.
void CUnit::DroneBuildStructure(int nArg0)
{
  count_remove_completion(this, 0);
  this->dwFlags &= ~1;
  TransformType(nArg0);
  town_rejoin_worker(this);
  DroneBuild();
}

// TRUE when this zerg building is mid-morph into one of the upgraded types.
BOOL CUnit::IsZergBldgUpgrading()
{
  return (this->dwFlags & 1) == 0
      && type_is_morphed_structure(this->wBuildQueue[this->bBuildQueueSlot]);
}

// Adds the newly hatched unit to the local selection if there is a free slot.
void CUnit::SelectBirthedCopy()
{
  CUnit *ppUnits[SELECTION_WIDE];
  int i;

  // Hyperion: the selection's 12 or 255 (stage S, selectlimits.h)
  memset(ppUnits, 0, sizeof(ppUnits));
  memcpy(ppUnits, NewUnits(), SelectionMax() * sizeof(CUnit *));

  for (i = 0; i < SelectionMax(); ++i)
    if (ppUnits[i] == 0)
      break;

  if (i >= SelectionMax())
    return;

  ppUnits[i] = this;
  select_units(i + 1, ppUnits, 0, 1);
}

// Walks a tile rectangle and covers each free tile with creep. nArg4 rounds the
// corners off: with it set, a tile on both an edge column and an edge row is
// skipped. Redraws the minimap once if anything changed.
void CUnit::SpreadBldgCreep(int nArg0, int nArg1, int nArg2, int nArg3, int nArg4)
{
  int x;
  int y;
  int i;
  int j;
  int bChanged;

  y = nArg1;
  bChanged = 0;
  i = nArg3;
  while (i != 0)
  {
    --i;
    x = nArg0;
    if (static_cast<DWORD>(y) < gwMapHeight)
    {
      j = nArg2;
      while (j != 0)
      {
        --j;
        if (static_cast<DWORD>(x) < gwMapWidth)
        {
          if (nArg4 == 0
              || (j != nArg2 - 1 && j != 0)
              || (i != nArg3 - 1 && i != 0))
          {
            if (!tile_can_have_creep(static_cast<WORD>(x), static_cast<WORD>(y)))
            {
              if (!tile_is_occupied(static_cast<WORD>(x), static_cast<WORD>(y)))
              {
                if (cover_creep_at(x, y))
                  bChanged = 1;
              }
            }
          }
        }
        ++x;
      }
    }
    ++y;
  }

  if (bChanged)
    minimap_force_redraw();
}

// Clears the placement matrix under a new zerg building and spreads creep over
// its footprint. A creep colony family member covers a wider box.
void CUnit::ZergPlaceBldg(int nArg0, int nArg1, int nArg2)
{
  point16_t box;
  int nX;
  int nY;
  int nW;
  int nH;

  if (!unit_type_spreads_creep(nArg0, 1)
      && (gulUnitTypeFlags[static_cast<WORD>(nArg0)] & 0x20000))
    return;

  box = gUnitPlaceboxSize[static_cast<WORD>(nArg0)];
  nX = (nArg1 - box.x / 2) / 32;
  nY = (nArg2 - box.y / 2) / 32;
  nW = box.x / 32;
  nH = box.y / 32;
  mtx_unplace_bldg(nX, nY, nW, nH, 0x8000000);

  if (gulUnitTypeFlags[static_cast<WORD>(nArg0)] & 0x80000000)
    CUnit::SpreadBldgCreep(nX - 2, nY - 1, nW + 4, nH + 2, 1);
  else
    CUnit::SpreadBldgCreep(nX, nY, nW, nH, 0);

  mtx_place_bldg(nX, nY, nW, nH, 0x8000000);
}

// Lair, Hive, Greater Spire, Spore Colony, Sunken Colony -- the units.dat types
// reached by morphing another building.
BOOL type_is_morphed_structure(int nArg0)
{
  return nArg0 == 0x85 || nArg0 == 0x84 || nArg0 == 0x89
      || nArg0 == 0x90 || nArg0 == 0x92;
}
