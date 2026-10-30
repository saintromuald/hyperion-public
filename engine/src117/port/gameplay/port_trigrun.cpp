// port_trigrun.cpp -- 1.17's trigger actions run outside a trigger (port_trigrun.h): the live
// channel's spawn, order and clear (port_control.cpp) and the cutscenes' steps
// (port_cutscene.cpp) go through here, so the game sees what a trigger would have done. The
// scratch locations are the last two of the map's 255, which the canvas and the maps a
// sandbox is used on leave free.
#include <windows.h>
#include <string.h>
#include <algorithm>
#include <vector>
#include "../tools/port_control.h"   // PortControlChanged
#include "port_trigrun.h"
#include "oc_data.h"
#include "pos.h"            // PosS
#include "TrigAct.h"        // TrigAction, the actions, show_portrait, map_msg_default
#include "TrigEnum.h"       // MapRegion, TrigSlotTag, clear_enum_cache
#include "CUnit.h"
#include "CSprite.h"
#include "minimap.h"        // do_minimap_ping
#include "snd.h"            // play_digital
#include "playertables.h"   // PlayerSlots
#include "playerlimits.h"   // PLAYER_SLOTS_117
#include "poollimits.h"     // g_pools
#include "typelimits.h"     // UnitTypeValid
#include "../../gen/globals117.h"

extern DWORD sgdwTriggerPlayer;     // 0x6CA754, the trigger player (victory.cpp)

namespace {

const WORD ANY_UNIT = 229;
const DWORD GROUP_CURRENT = 13;

// A slot past 11 has no trigger player code of its own: it is named as the current player,
// which the actions read while they run. TriggerAs sets that up and ~TriggerAs puts it back.
struct TriggerAs {
  DWORD dwSaved, dwGroup;
  explicit TriggerAs(int nPlayer) : dwSaved(sgdwTriggerPlayer), dwGroup(static_cast<DWORD>(nPlayer))
  {
    if (nPlayer >= PLAYER_SLOTS_117) {
      sgdwTriggerPlayer = static_cast<DWORD>(nPlayer);
      dwGroup = GROUP_CURRENT;
    }
  }
  ~TriggerAs() { sgdwTriggerPlayer = dwSaved; }
};

}  // namespace

CUnit *PortTrigUnit(int nId)
{
  if (nId < 0 || nId >= g_pools.units)
    return NULL;
  CUnit *pWanted = CUnit::Head() + nId;
  for (CUnit *pUnit = CUnit::GetUsedList(); pUnit != NULL; pUnit = pUnit->pNext)
    if (pUnit == pWanted)
      return pUnit->IsDeadOrDying() ? NULL : pUnit;
  return NULL;
}

int PortTrigScratch(int nSlot, int x, int y, int nRadius)
{
  const int i = 254 - nSlot;
  gMapRegions[i].left = x - nRadius;
  gMapRegions[i].top = y - nRadius;
  gMapRegions[i].right = x + nRadius;
  gMapRegions[i].bottom = y + nRadius;
  gMapRegions[i].wFlags = 0;  // every elevation, ground and air
  return i + 1;
}

bool PortTrigCreateFits(int nUnit, int x, int y, int nPlayer, int nCount)
{
  return nUnit >= 0 && UnitTypeValid(nUnit) && x >= 0 && y >= 0 && x < gwMapWidth * 32 && y < gwMapHeight * 32 &&
         nPlayer >= 0 && nPlayer < PlayerSlots() && nCount >= 1 && nCount <= 64;
}

void PortTrigCreate(int nUnit, int x, int y, int nPlayer, int nCount, std::vector<int> *pIds, bool bChanged)
{
  if (!PortTrigCreateFits(nUnit, x, y, nPlayer, nCount))
    return;
  std::vector<CUnit *> before;  // to name the units made
  if (pIds != NULL) {
    for (CUnit *pUnit = CUnit::GetUsedList(); pUnit != NULL; pUnit = pUnit->pNext)
      before.push_back(pUnit);
    std::sort(before.begin(), before.end());
  }
  {
    TriggerAs as(nPlayer);
    TrigAction a;
    memset(&a, 0, sizeof a);
    a.bRegion = static_cast<BYTE>(PortTrigScratch(0, x, y, 16));
    a.dwGroup = as.dwGroup;
    a.wUnitType = static_cast<WORD>(nUnit);
    a.bCount = static_cast<BYTE>(nCount);
    act_create_unit(&a);
  }
  if (bChanged)
    PortControlChanged();
  if (pIds != NULL)
    for (CUnit *pUnit = CUnit::GetUsedList(); pUnit != NULL; pUnit = pUnit->pNext)
      if (!std::binary_search(before.begin(), before.end(), pUnit))
        pIds->push_back(static_cast<int>(pUnit - CUnit::Head()));
}

void PortTrigOrderNear(int nPlayer, int x, int y, int nRadius, int tx, int ty, int nOrder)
{
  if (nPlayer < 0 || nPlayer >= PlayerSlots())
    return;
  TriggerAs as(nPlayer);
  TrigAction o;
  memset(&o, 0, sizeof o);
  o.bRegion = static_cast<BYTE>(PortTrigScratch(0, x, y, nRadius));
  o.dwGroup = as.dwGroup;
  o.dwValue = static_cast<DWORD>(PortTrigScratch(1, tx, ty, 16));
  o.wUnitType = ANY_UNIT;
  o.bCount = static_cast<BYTE>(nOrder);
  act_order(&o);
  PortControlChanged();
}

bool PortTrigOrderId(int nId, int tx, int ty, int nOrder)
{
  CUnit *pUnit = PortTrigUnit(nId);
  if (pUnit == NULL)
    return false;
  PortTrigOrderOne(pUnit, tx, ty, nOrder);
  PortControlChanged();
  return true;
}

int PortTrigRemoveAll(int nPlayer)
{
  if (nPlayer < 0 || nPlayer >= PlayerSlots())
    return 0;
  int n = 0;
  for (CUnit *pUnit = CUnit::GetUsedList(); pUnit != NULL; pUnit = pUnit->pNext)
    n += pUnit->bPlayer == nPlayer;
  TrigAction a;
  memset(&a, 0, sizeof a);
  a.dwGroup = TrigSlotTag(static_cast<DWORD>(nPlayer));
  a.wUnitType = ANY_UNIT;
  act_remove(&a);
  PortControlChanged();
  return n;
}

bool PortTrigRemoveId(int nId, bool bKill)
{
  CUnit *pUnit = PortTrigUnit(nId);
  if (pUnit == NULL)
    return false;
  // as Kill Unit and Remove Unit do to each unit they take (TrigAct.cpp)
  if (pUnit->IsATransport() && !bKill)
    pUnit->SelfDestruct();
  else {
    if (!bKill)
      pUnit->Hide();
    pUnit->Suicide();
  }
  clear_enum_cache();
  PortControlChanged();
  return true;
}

void PortTrigTransmission(int nPortrait, int nFlashId, const char *pszText, const char *pszSound, unsigned nMs)
{
  if (pszSound != NULL && pszSound[0] != '\0')
    PortTrigSound(pszSound);
  CUnit *pUnit = PortTrigUnit(nFlashId);
  if (pUnit != NULL) {
    pUnit->pSprite->bFlashCounter = 0x2D;
    do_minimap_ping(PosS(pUnit->pSprite->wPosX), pUnit->pSprite->wPosY, 0x11);
  }
  if (nPortrait >= 0)
    show_portrait(static_cast<WORD>(nPortrait), pUnit != NULL ? PosS(pUnit->pSprite->wPosX) : -1,
                  pUnit != NULL ? static_cast<int>(pUnit->pSprite->wPosY) : -1, nMs);
  if (pszText != NULL && pszText[0] != '\0') {
    DWORD dwTime = nMs;
    if (dwTime <= static_cast<DWORD>(Trig_GetTextDisplayTime(pszText)))
      dwTime = Trig_GetTextDisplayTime(pszText);
    map_msg_default(pszText, dwTime);
  }
}

void PortTrigText(const char *pszText, unsigned nMs)
{
  if (pszText == NULL || pszText[0] == '\0')
    return;
  map_msg_default(pszText, nMs != 0 ? static_cast<int>(nMs) : Trig_GetTextDisplayTime(pszText));
}

void PortTrigSound(const char *pszPath)
{
  if (pszPath != NULL && pszPath[0] != '\0')
    play_digital(pszPath);
}
