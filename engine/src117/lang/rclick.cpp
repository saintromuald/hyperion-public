// rclick.cpp -- the right-click order query: what order a click on a target
// produces.
//
// Bodies are in ascending oracle address, 1.17's own order.

#include "oc_data.h"  // Hyperion: OC_VA
#include "../h/rclick.h"
#include "../h/order.h"          // ours: status_get_str + map_msg_target_self
#include "../h/CUnitTSpells.h"  // ours: gamesnd_play
#include "../h/CThingy.h"       // ours: sub_417F70, CThingy::pSprite
#include "../h/CSprite.h"       // ours: CSprite::bFlashCounter
#include "../h/minimap.h"       // ours: AllocBlackX
#include "../h/dialog.h"        // ours: dlgevt_t
#include "../h/net_cmds.h"      // ours: send_net_cmd_RightClick
#include "../h/gamesnd.h"       // ours: gamesnd_ack
#include "../h/usel.h"          // ours: get_map_target, console_collision
#include "../h/select.h"        // ours: select_form_t, AllowedErr
#include "../h/statbtn.h"       // ours: OrderAllowed
#include "../gen/globals117.h"
#include "../port/render/port_view.h"  // Hyperion: PortViewScreenToMap
#include "../h/selectlimits.h"  // Hyperion: SELECTION_MAX

// The globals this TU reaches, each spelled once for the standalone build and
// once for the payload.

// 0x665E68 -- gKeyTbl[VK_SHIFT], the shift key state byte (event_Win.cpp owns
// the table and spells this entry byte_665E68).
#ifndef byte_665E68
#ifdef OC_DLL_MODE
#define byte_665E68 (*reinterpret_cast<BYTE *>(OC_VA(0x665E68u)))
#else
extern BYTE byte_665E68;
#endif
#endif

// 0x47EF40. The query for a unit that cannot act on a right click.
int rclick_query_none(CUnit *, int, int, CUnit **, WORD)
{
  return 0x17;
}

// 0x47EF50. The general query: hold position wins, an empty click picks between
// move and the rally order, and a live target picks attack, load or move.
int rclick_query_attacker(CUnit *pUnit, int, int, CUnit **ppTarget, WORD wUnitType)
{
  CUnit *pTarget = *ppTarget;

  if (pUnit->dwFlags & 2)
    return 0x17;

  if (pTarget == NULL)
    return wUnitType != 0xE4 ? 0x85 : 6;

  if (pTarget->IsCircleOfPower() || (gulUnitTypeFlags[pTarget->wType] & 0x800))
    return 6;

  if (pUnit->IsEnemyUnit(pTarget))
    return 8;

  if (pTarget->IsATransport() && pUnit->CanEnterTransport(pTarget))
    return 0x5C;

  return (pTarget->dwFlags & 0x10) ? 6 : 0x31;
}

// 0x47F010. A tower's right-click query. An enemy target that is not a circle
// of power and does not carry unit flag 0x800 yields order 8; everything else
// falls through to the clicking unit's own gubUnitOrderGuard row.
int rclick_query_tower(CUnit *pUnit, int, int, CUnit **ppTarget, WORD)
{
  CUnit *pTarget = *ppTarget;

  if (pTarget != NULL && !pTarget->IsCircleOfPower() &&
      (gulUnitTypeFlags[pTarget->wType] & 0x800) == 0 &&
      pUnit->IsEnemyUnit(pTarget))
    return 8;

  return gubUnitOrderGuard[pUnit->wType];
}

// 0x47F070. The query for a building that can move or rally.
int rclick_query_mover(CUnit *pUnit, int, int, CUnit **ppTarget, WORD wUnitType)
{
  CUnit *pTarget;

  if (pUnit->dwFlags & 2) {
    if (pUnit->IsRallyer())
      return *ppTarget != NULL ? 0x27 : 0x28;
    return 0x17;
  }

  pTarget = *ppTarget;
  if (pTarget == NULL) {
    if ((pUnit->wType == 0x2D || pUnit->wType == 0x31) && wUnitType != 0xE4)
      return 0x85;
    return 6;
  }

  if (pUnit->IsATransport() && pUnit->ErrorRClick(0x5E, pTarget, 0, 0, NULL))
    return 0x5E;

  if (pTarget->IsATransport() && pUnit->CanEnterTransport(pTarget))
    return 0x5C;

  if (pTarget->dwFlags & 0x10)
    return 6;

  if ((pUnit->wType == 0x2D || pUnit->wType == 0x31) &&
      pUnit->sub_434130(0x0C, pTarget, 0xE4, 0, 0, NULL))
    return 0x1B;

  return pUnit->wType == 0x22 ? 0xB1 : 0x31;
}

// 0x47F180. The worker query that can also repair: the harvest arm first, then
// the repair test, then load, then the general query.
int rclick_query_scv(CUnit *pUnit, int nX, int nY, CUnit **ppTarget, WORD wUnitType)
{
  CUnit *pTarget = *ppTarget;
  int nOrder;

  if (pTarget == NULL)
    return rclick_query_attacker(pUnit, nX, nY, ppTarget, wUnitType);

  nOrder = rclick_query_harvester(pUnit, nX, nY, pTarget);
  if (nOrder != 0xBD)
    return nOrder;

  if ((pTarget->dwFlags & 2) != 0 && (pTarget->dwFlags & 1) == 0) {
    if (pTarget->bPlayer == pUnit->bPlayer && pTarget->GetRace() == 1 &&
        (gulUnitTypeFlags[pTarget->wType] & 2) == 0 &&
        (pTarget->pUnion80 == NULL || pTarget->pUnion80->pOrderTarget != pTarget))
      return 0x21;
  } else if (sgbAlliances_g[pUnit->bPlayer][pTarget->bPlayer] >= 1) {
    if ((gulUnitTypeFlags[pTarget->wType] & 1) == 0 && pTarget->IsATransport() &&
        pUnit->CanEnterTransport(pTarget))
      return 0x5C;

    if (pUnit->ErrorRClick(0x22, pTarget, PosU(nX), PosU(nY), NULL))
      return 0x22;

    if (pTarget->IsATransport() && pUnit->CanEnterTransport(pTarget))
      return 0x5C;
  }

  return rclick_query_attacker(pUnit, nX, nY, ppTarget, wUnitType);
}

// 0x47F2C0. The plain worker query: the harvest arm, then the general query.
int rclick_query_worker(CUnit *pUnit, int nX, int nY, CUnit **ppTarget, WORD wUnitType)
{
  CUnit *pTarget = *ppTarget;

  if (pTarget != NULL) {
    int nOrder = rclick_query_harvester(pUnit, nX, nY, pTarget);
    if (nOrder != 0xBD)
      return nOrder;
  }

  return rclick_query_attacker(pUnit, nX, nY, ppTarget, wUnitType);
}

// 0x47F300. The default right click on the game map: when the local player's
// selection can act, sends the click at the map position with the unit or
// thingy under it, and acknowledges it if rclick_interpret accepts it.
// x before y, so y lands in esi and in the [ebp-4] spill as in the oracle.
void __fastcall gamemap_rclick_default(dlgevt_t *pEvent)
{
  if (console_collision(pEvent->nX, pEvent->nY))
    return;
  if (GetSelectedOwner() != g_LocalNationID)
    return;
  if (gpStatUnit == NULL || gPlayback || gpStatUnit->bPlayer != g_LocalNationID)
    return;
  int i;
  for (i = 0; i < SelectionMax(); i++) {
    if (StatUnits()[i] && unit_has_rclick_action(StatUnits()[i]))
      break;
  }
  if (i < SelectionMax()) {
    // Hyperion: through the camera (port_view.cpp)
    int x, y;
    PortViewScreenToMap(pEvent->nX, pEvent->nY, &x, &y);
    CUnit *pTarget = get_map_target(x, y);
    if (byte_6AB4C0 == 1 && gpStatUnit == pTarget) {
      if (pTarget == NULL || !(pTarget->dwFlags & 2) || !pTarget->IsRallyer())
        return;
    }
    if (pTarget == NULL) {
      for (i = 0; i < SelectionMax(); i++) {
        CUnit *pUnit = StatUnits()[i];
        if (pUnit) {
          switch (pUnit->GetRClickAction()) {
          case 1:
          case 2:
          case 4:
          case 5:
          case 6:
            goto found;
          }
        }
      }
found:
      if (i >= SelectionMax())
        return;
    }
    CThingy *pThingy = sub_417F70(x, y);
    int nType;
    if (pThingy != NULL) {
      pThingy->pSprite->bFlashCounter = 0x1F;
      nType = pThingy->wUnitType;
    } else {
      if (pTarget != NULL)
        pTarget->pSprite->bFlashCounter = 0x1F;
      else
        AllocBlackX(x, y);
      nType = 0xE4;
    }
    send_net_cmd_RightClick(x, y, pTarget, nType, byte_665E68);
    if (rclick_interpret(pTarget))
      gamesnd_ack(gpStatUnit);
  }
}

// 0x47F4B0. Whether a right click on this unit can produce an order at all:
// disabled, locked down, stasised or paralysed units cannot, and neither can a
// few type/order pairs.
BOOL unit_has_rclick_action(CUnit *pUnit)
{
  DWORD dwFlags = pUnit->dwFlags;

  // One chain with the `return 0` textually last: as eight early returns MSVC
  // puts the shared exit after the first test and branches backwards into it.
  if ((dwFlags & 0x400) == 0 &&
      pUnit->bLockdownTimer == 0 &&
      pUnit->bStasisTimer == 0 &&
      pUnit->bParalyzeTimer == 0 &&
      ((dwFlags & 0x10) == 0 || pUnit->wType == 0x67) &&
      !(pUnit->wType == 0x07 && pUnit->bOrderId == 0x21) &&
      !((pUnit->wType == 0x01 || pUnit->wType == 0x10 ||
         pUnit->wType == 0x64 || pUnit->wType == 0x63 ||
         pUnit->wType == 0x68) && pUnit->bOrderId == 0x81) &&
      !(pUnit->wType == 0x44 && pUnit->bOrderId == 0x6A))
    return pUnit->GetRClickAction() != 0;

  return 0;
}

// query_click @0x508E18, one right-click query per GetRClickAction result.
typedef int (__fastcall *rclick_query_fn)(CUnit *pUnit, int nX, int nY,
                                          CUnit **ppTarget, WORD wUnitType);
#ifndef OC_query_click
#define OC_query_click
#ifdef OC_DLL_MODE
#define query_click (reinterpret_cast<rclick_query_fn *>(OC_VA(0x508E18u)))
#else
extern rclick_query_fn query_click[7];
#endif
#endif

// byte_67CC68, the secondary order a query may leave behind (0x17 = none).
#ifndef OC_byte_67CC68
#define OC_byte_67CC68
#ifdef OC_DLL_MODE
#define byte_67CC68 (*reinterpret_cast<BYTE *>(OC_VA(0x67CC68u)))
#else
extern BYTE byte_67CC68;
#endif
#endif

// 0x47F680. Executes a received right click for every selected unit: runs the
// unit's query, then sets a rally or issues the order (and any secondary order)
// on the target or at the formation point. The repeated test in the loop
// condition is what keeps MSVC from padding the loop head.
void __fastcall sub_47F680(int nX, int nY, CUnit *pTarget, int nTargetType, BYTE bQueued)
{
  select_form_t form;
  form.ptTarget.x = nX;
  form.ptTarget.y = nY;
  select_form_calc_offset(&form, 1);
  int nType;
  if (pTarget != NULL) {
    nType = 0xE4;
  } else {
    nType = nTargetType;
    if ((WORD)nType != 0xE4 &&
        (gpSquareMap[(nY / 32) * gwMapWidth + nX / 32] & (1 << g_ActiveNationID)) == 0) {
      pTarget = GetRClickTarget(nX, nY, nType);
      nType = 0xE4;
    }
  }
  // Declared outside the loop it gets [ebp-8]; inside, it takes nTargetType's home.
  CUnit *pClick;
  for (CUnit *pUnit = select_get_first(); pUnit != NULL && pUnit != NULL; pUnit = select_get_next()) {
    BYTE bAction = pUnit->GetRClickAction();
    if ((pUnit->dwFlags & 0x40000000) && (bAction == 4 || bAction == 5))
      bAction = 1;
    pClick = pTarget;
    byte_67CC68 = 0x17;
    order_id_t bOrder = (BYTE)query_click[bAction](pUnit, nX, nY, &pClick, nType);
    if (bOrder == 0x17)
      continue;
    if (bOrder == 0x27) {
      if (pUnit->IsRallyer())
        pUnit->SetRallyTarget_CUnit(pTarget);
      continue;
    }
    if (pUnit == pTarget)
      continue;
    if (bOrder == 0x28) {
      if (pUnit->IsRallyer())
        pUnit->SetRallyTarget_xy(nX, nY);
      continue;
    }
    if (bOrder == 8) {
      CUnit *pSub = pUnit->subunit;
      if (pSub != NULL)
        pSub->sub_42A880(bQueued != 0, gubUnitOrderAttackUnit[pSub->wType], pClick);
      bOrder = gubUnitOrderAttackUnit[pUnit->wType];
    }
    if (OrderAllowed(bOrder, g_ActiveNationID, pUnit) != 1) {
      AllowedErr(get_allowed_err());
      continue;
    }
    if (pClick != NULL) {
      int nEnable = bQueued != 0;
      pUnit->sub_42A880(nEnable, bOrder, pClick);
      if (byte_67CC68 != 0x17)
        pUnit->sub_42A880(nEnable, byte_67CC68, pClick);
    } else {
      form.ptTarget.x = nX;
      form.ptTarget.y = nY;
      if ((WORD)nType == 0xE4)
        pUnit->sub_49C5A0(&form);
      else
        form.ptUnit = form.ptTarget;
      int nEnable = bQueued != 0;
      pUnit->sub_42A850(nEnable, bOrder, nType, form.ptUnit.x, form.ptUnit.y);
      if (byte_67CC68 != 0x17)
        pUnit->sub_42A850(nEnable, byte_67CC68, nType, form.ptUnit.x, form.ptUnit.y);
    }
    pUnit->bUnion93 |= 2;
  }
}
#undef nType

// A clamped map position handed to sub_46CFD0 by address; x is the low slot.
struct rclick_point32_t {
  /*0x00*/ LONG x;
  /*0x04*/ LONG y;
};
static_assert(offsetof(rclick_point32_t, x) == 0x00, "rclick_point32_t.x");
static_assert(offsetof(rclick_point32_t, y) == 0x04, "rclick_point32_t.y");
static_assert(sizeof(rclick_point32_t) == 0x08, "rclick_point32_t is two dwords");

// 0x47F8D0. A right click on the minimap: when the local player's selection
// holds a unit that can act on it, sends the click at the clamped map position.
void __fastcall rclick_action(dlgevt_t *pEvent)
{
  if (gpStatUnit == NULL || gpStatUnit->bPlayer != g_LocalNationID)
    return;
  int i;
  for (i = 0; i < SelectionMax(); i++) {
    if (StatUnits()[i] && unit_has_rclick_action(StatUnits()[i]))
      break;
  }
  if (i < SelectionMax()) {
    for (i = 0; i < SelectionMax(); i++) {
      CUnit *pUnit = StatUnits()[i];
      if (pUnit) {
        switch (pUnit->GetRClickAction()) {
        case 1:
        case 2:
        case 4:
        case 5:
        case 6:
          goto found;
        }
      }
    }
found:
    if (i < SelectionMax()) {
      rclick_point32_t pt;
      pt.x = PosU(pEvent->nX);
      pt.y = PosU(pEvent->nY);
      sub_46CFD0(&pt.x, &pt.y);
      send_net_cmd_RightClick(PosU(pt.x), PosU(pt.y), NULL, 0xE4, byte_665E68);
      gamesnd_ack(gpStatUnit);
      AllocBlackX(pt.x, pt.y);
    }
  }
}

// 0x47F9D0. Walks the selection from the last slot down and picks the ack sound
// and error string a right click on pTarget should produce. Returning TRUE means
// the click issues an order instead.
BOOL rclick_interpret(CUnit *pTarget)
{
  int nSound = -1;
  int nStr = -1;
  int i;

  for (i = SelectionMax() - 1; i >= 0; i--) {  // Hyperion: selectlimits.h's (stage S)
    CUnit *pUnit = StatUnits()[i];
    BYTE bAction;

    if (pUnit == NULL)
      continue;

    bAction = pUnit->GetRClickAction();
    if ((pUnit->dwFlags & 0x40000000) && (bAction == 4 || bAction == 5))
      bAction = 1;

    // The cases are in the oracle's own block order, 4/5, 1, 3, 0, 2, and 4/5
    // falls through into 1.
    switch (bAction) {
    case 4:
    case 5:
      if (pTarget == NULL)
        return TRUE;
      if (gulUnitTypeFlags[pTarget->wType] & 0x800)
        return TRUE;
      if (pTarget->wType == 0xBC) {
        nSound = pUnit->GetRace() + 1;
        nStr = (WORD)(pUnit->GetRace() + 0x37D);
        break;
      }
      // fall through
    case 1:
      if (pTarget == NULL)
        return TRUE;
      if (!gpStatUnit->IsEnemyUnit(pTarget))
        return TRUE;
      if (pTarget->IsCircleOfPower())
        return TRUE;
      if (gulUnitTypeFlags[pTarget->wType] & 0x800)
        return TRUE;
      if (pUnit->CanTarget(pTarget, 1))
        return TRUE;
      nStr = 0x36F;
      nSound = pUnit->GetRace() + 0x8A;
      break;
    case 3:
      if (pTarget == NULL)
        break;
      if (pTarget->IsCircleOfPower())
        break;
      if (gulUnitTypeFlags[pTarget->wType] & 0x800)
        break;
      if (!pUnit->CanTarget(pTarget, 1)) {
        nSound = pUnit->GetRace() + 0x8A;
        nStr = 0x36F;
        break;
      }
      if (pUnit->sub_420AB0(pTarget)) {
        nSound = pUnit->GetRace() + 0x8A;
        nStr = 0x378;
        break;
      }
      if (!pUnit->sub_420B50(pTarget))
        return TRUE;
      nSound = pUnit->GetRace() + 0x8A;
      nStr = 0x379;
      break;
    case 0:
      break;
    case 2:
    default:
      return TRUE;
    }
  }

  if (nSound != -1)
    gamesnd_play(nSound, NULL, 1, 0);

  if ((short)nStr != -1)
    map_msg_target_self(status_get_str((WORD)nStr), g_LocalNationID);

  return FALSE;
}

// 0x47FC10. Flashes the marker at a clicked position: an existing thingy there,
// else the target unit's sprite, else a fresh black X. The else-if chain keeps
// the NULL result in a register; early returns fold it to `xor eax, eax`.
CThingy *show_target_x(int x, int y, CUnit *pTarget)
{
  CThingy *pThingy = sub_417F70(x, y);
  if (pThingy != NULL)
    pThingy->pSprite->bFlashCounter = 0x1F;
  else if (pTarget != NULL)
    pTarget->pSprite->bFlashCounter = 0x1F;
  else
    AllocBlackX(x, y);
  return pThingy;
}

// 0x47FC60. The harvest arm shared by the two worker queries. 0xBD is the
// "nothing to harvest here" sentinel both callers test for.
int rclick_query_harvester(CUnit *pUnit, int, int, CUnit *pTarget)
{
  if (gulUnitTypeFlags[pTarget->wType] & 0x800)
    return 6;

  if (pTarget->IsTargetMineralSrc(pUnit)) {
    BOOL bFull = (pUnit->bCarryResource != 0 &&
                  (pUnit->bCarryResource & 3) == 0);
    return bFull == 0 ? 0x55 : 6;
  }

  if (!pTarget->IsTargetRefinery(pUnit) && pTarget->wType != 0xBC) {
    if (pTarget->IsResourceDest() && pTarget->bPlayer == pUnit->bPlayer) {
      if (pUnit->bCarryResource & 2)
        return 0x5A;
      if (pUnit->bCarryResource & 1)
        return 0x54;
    }
    return 0xBD;
  }

  {
    BOOL bFull = (pUnit->bCarryResource != 0 &&
                  (pUnit->bCarryResource & 3) == 0);
    return bFull == 0 ? 0x51 : 6;
  }
}
