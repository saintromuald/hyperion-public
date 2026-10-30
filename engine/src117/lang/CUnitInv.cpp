// CUnitInv.cpp -- unit inventory and powerup dispatch. Bodies are in oracle VA order.
//
// A callee defined here can be spliced into a caller here by /Ob2, which the oracle (WPO
// off) never does across a TU; that is what the __declspec(noinline) markers on some
// declarations are for, and why moving a body between TUs can change the code generated.

#include "oc_data.h"  // Hyperion: OC_VA
#include "../h/playertables.h"  // Hyperion: player slots past 11
#include "../h/CUnitInv.h"
#include "../h/CUnit.h"
#include "../h/CSprite.h"
#include "../h/CImage.h"
#include "../h/point16.h"           // FindPlaceForUnit's in/out pair
#include "../h/rect16.h"            // its bounds argument
#include "../h/CUnitCOP.h"          // ctf_flag_taken, ctf_flag_returned
#include "../h/CUnitTerran.h"       // status_set_update
#include "../h/CUnitZSpells.h"      // FindPlaceForUnit
#include "../h/CUnitPathUtil.h"     // sub_42ED50
#include "../h/collide.h"           // mtx_has_flags_px
#include "../h/SAI_Banker.h"        // banker_add_current_resources
#include "../gen/globals117.h"

// The globals this TU reaches, dual-spelled: one #ifdef per name, never one over the
// block, so adding a name cannot undeclare the ones below it.

// gPlayerMinerals @0x5263D0, gPlayerGas @0x526400 and sgMapPixRect_g @0x543228
// are registered but gen does not emit them, so they are spelled here the way
// banker.cpp spells the first two. The two counters DepositInventory feeds
// beside the balances have no sheet row and no registry row, so they keep
// positional spellings.
#ifdef OC_DLL_MODE
  #ifndef gPlayerMinerals
  #define gPlayerMinerals (reinterpret_cast<int *>(OC_VA(0x5263D0u)))
  #endif
  #ifndef gPlayerGas
  #define gPlayerGas (reinterpret_cast<int *>(OC_VA(0x526400u)))
  #endif
  #ifndef dword_526430
  #define dword_526430 (reinterpret_cast<int *>(OC_VA(0x526430u)))
  #endif
  #ifndef dword_526460
  #define dword_526460 (reinterpret_cast<int *>(OC_VA(0x526460u)))
  #endif
  #ifndef sgMapPixRect_g
  #define sgMapPixRect_g (*reinterpret_cast<mrect_t *>(OC_VA(0x543228u)))
  #endif
#else
extern PlayerMineralsTable gPlayerMinerals;
extern PlayerGasTable gPlayerGas;
extern PlayerGasGatheredTable dword_526430;
extern PlayerMineralsGatheredTable dword_526460;
extern mrect_t sgMapPixRect_g;  // Hyperion: a box of map positions
#endif

// CUnit +0xD0/+0xD2, the carried powerup's saved position. CUnit.h's union at
// +0xD0 carries no signed-16 arm, so both are reached by offset;
// DispatchPowerupCTF copies the pair out as one point16_t.
#define UNIT_DROP_X(u)   (*reinterpret_cast<short *>(reinterpret_cast<BYTE *>(u) + offsetof(CUnit, pUnionD0)))
#define UNIT_DROP_Y(u)   (*reinterpret_cast<short *>(reinterpret_cast<BYTE *>(u) + offsetof(CUnit, pUnionD0) + 2))

// The bodies, in oracle VA order.

// Attaches a carried-resource overlay and records how much is carried.
// bCarryResource comes out 1 or 2, which is what `setnz al / inc al` encodes.
void CUnit::AddInventory(int nArg0, BYTE bArg1, BYTE bArg2)
{
  CImage *pImage;

  if (bCarryResource != 0)
    return;

  bCarryResource = (bArg2 != 0) + 1;

  pImage = pSprite->AllocImage(nArg0, 0, 0, 0);
  if (pImage != NULL && (pImage->flags.w & 0x80) == 0)
  {
    pImage->flags.w |= 0x80;
    pImage->ApplySpecialOffset();
  }

  _unmapped_CF = bArg1;
  status_set_update();
}

// Picks a unit up into this unit's powerup slot.
void CUnit::AddInventory_CUnit(void *pArg0)
{
  CUnit *pItem = static_cast<CUnit *>(pArg0);
  WORD wItemType;
  int nImageId;
  CImage *pImage;

  if (bCarryResource != 0)
  {
    if (pPowerup != NULL)
      return;

    RemoveInventoryOverlay();
    bCarryResource = 0;
  }

  if (pItem->wType == 0xD7)
    ctf_flag_taken(pItem->bPlayer);

  wItemType = pItem->wType;
  switch (wItemType)
  {
    case 0x80:
      nImageId = 0x3BE;
      break;
    case 0x81:
      nImageId = 0x3BF;
      break;
    default:
      nImageId = wItemType + 0xB1;
      break;
  }

  pItem->Hide();
  pItem->SetOrder(0x17);
  pItem->pUnionD4 = this;

  pImage = pSprite->AllocImage(nImageId, 0, 0, 0);
  if (pImage != NULL)
  {
    if (pImage->bPaletteType == 0x0E)
      pImage->SetRLEPlayer(pItem->bPlayer);

    if ((pImage->flags.w & 0x80) == 0)
    {
      pImage->flags.w |= 0x80;
      pImage->ApplySpecialOffset();
    }
  }

  bCarryResource |= 4;
  pPowerup = pItem;
  status_set_update();
}

void CUnit::DestroyInventory()
{
  if (bCarryResource != 0)
  {
    RemoveInventoryOverlay();

    if (pPowerup != NULL)
      pPowerup->SelfDestruct();

    bCarryResource = 0;
    pPowerup = NULL;
  }
}

// The flag carrier's per-tick order: pick a flag up, then run the return
// countdown and finally teleport the flag home. The home position is copied
// out as a point16_t: a DWORD split swaps EAX and ECX over the load.
void CUnit::DispatchPowerupCTF()
{
  CUnit *pPickup;

  if (pSprite->flags.b & 0x20)
    Show();

  pPickup = sub_431C20();
  if (pPickup != NULL)
  {
    pPickup->AddInventory_CUnit(this);
    status_set_update();
  }

  if (bOrderState == 0)
  {
    bOrderState = 1;
    wRemainingBuildTime = 0x151;
    return;
  }

  if (wRemainingBuildTime != 0)
  {
    --wRemainingBuildTime;
    return;
  }

  point16_t pos = *reinterpret_cast<point16_t *>(&UNIT_DROP_X(this));
  SetUnitPosition(pos.x, pos.y);
  ctf_flag_returned(bPlayer);
  SetOrder(0x61);
}

void CUnit::DispatchPowerup()
{
  CUnit *pPickup;

  if (pSprite->flags.b & 0x20)
    Show();

  pPickup = sub_431C20();
  if (pPickup != NULL)
  {
    pPickup->AddInventory_CUnit(this);
    status_set_update();
  }
}

// Drops the carried powerup back onto the map, at the carrier's own spot when
// that tile is still free and at the powerup's saved position otherwise.
void CUnit::DropInventory()
{
  BYTE bCarry = bCarryResource;
  CUnit *pItem;
  mpoint_t  in;
  mpoint_t  out;
  int bAtDropSpot;
  int sX;
  int sY;

  if (bCarry == 0)
    return;

  if ((bCarry & 3) != 0)
    return;

  pItem = pPowerup;
  RemoveInventoryOverlay();

  sX = PosS(pSprite->wPosX);
  sY = pSprite->wPosY;
  bAtDropSpot = 1;

  if (!mtx_has_flags_px(sX, sY, -1, 0x8000000) ||
      !sub_42ED50(pItem->wType, sX, sY))
  {
    sX = UNIT_DROP_X(pItem);
    sY = UNIT_DROP_Y(pItem);
    bAtDropSpot = 0;
  }

  in.x = PosS(sX);
  in.y = PosS(sY);
  out.x = PosS(sX);
  out.y = PosS(sY);
  // The search clamps the box it is given, and this is the map's own, which keeps the clamp.
  FindPlaceForUnit(pItem, &in, &out, &sgMapPixRect_g, 0, 0);

  pItem->SetUnitPosition(out.x, out.y);
  if (pItem->wType == 0xD7 && bAtDropSpot)
    pItem->SetOrder(0x11);
  else
    pItem->SetOrder(0x61);
  pItem->pUnionD4 = NULL;

  bCarryResource = 0;
  pPowerup = NULL;
}

BOOL CUnit::HasFlag()
{
  if (gulUnitTypeFlags[wType] & 8)
  {
    CUnit *pHeld = pPowerup;

    return pHeld != NULL && pHeld->wType == 0xD7;
  }

  if (IsATransport())
    return IsFlagInside();

  return FALSE;
}

// Kills whichever carried overlay is present and reports the powerup overlay's
// image id, or 0x3E7 when there was none. The second and third lookups share
// one tail, so only the third result reaches the id.
WORD CUnit::RemoveInventoryOverlay()
{
  CImage *pImage;
  WORD wResult;

  pImage = pSprite->OverlayGetImage(0x195, 0x1A2);
  if (pImage != NULL)
    pImage->Kill();

  pImage = pSprite->OverlayGetImage(0x188, 0x194);
  if (pImage == NULL)
    pImage = pSprite->OverlayGetImage(0x3BE, 0x3BF);

  wResult = 0x3E7;
  if (pImage != NULL)
  {
    wResult = pImage->wImageID;
    pImage->Kill();
  }

  return wResult;
}

// Banks a carried resource. Bit 1 of bCarryResource is ore, bit 0 is gas.
void CUnit::DepositInventory(intptr_t nArg0)
{
  BYTE bCarry;

  RemoveInventoryOverlay();

  bCarry = bCarryResource;
  if ((bCarry & 2) != 0)
  {
    gPlayerMinerals[bPlayer] += _unmapped_CF;
    dword_526460[bPlayer] += _unmapped_CF;
    banker_add_current_resources(bPlayer, _unmapped_CF, 0);
  }
  else if ((bCarry & 1) != 0)
  {
    gPlayerGas[bPlayer] += _unmapped_CF;
    dword_526430[bPlayer] += _unmapped_CF;
    banker_add_current_resources(bPlayer, 0, _unmapped_CF);
  }

  bCarryResource = 0;
  _unmapped_CF = 0;
  status_set_update();
}
