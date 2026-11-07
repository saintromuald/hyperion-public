// port_newbie.cpp -- the newbie helpers (port_newbie.h), for a player learning the game
// (the user, 2026-11-04).
//
// Three helpers, each a toggle in Settings' Newbie mode tab:
// - Train workers: a completed town hall with nothing training and supply to spare trains
//   its worker, as the player would click it.
// - Mine efficiently: each base's patches take up to three workers, each geyser three;
//   once a base is full, the workers that are free go to the next base with room, and to
//   the built geysers as well.
// - Research: each upgrade the player ticked in the tab is researched when a building
//   that can do it is free, level after level until it is done.
//
// Every action is the net command the player's own click would send (SelectOnlySet to
// aim it at one unit without taking the selection over), so a replay records the commands
// and plays them back. Nothing runs in a replay.
#include <windows.h>
#include <vector>
#include "CUnit.h"
#include "CSprite.h"
#include "port_newbie.h"
#include "../platform/port_settings.h"  // PortSettingNumber, the newbie tab's toggles
#include "port_mutators.h"  // MutatorsOfGame, the newbie mode's mutator
#include "../../h/net_cmds.h"    // SelectOnlySet, send_net_cmd_*
#include "../../h/count.h"       // count_get_supplies_used, count_get_supplies_available
#include "../../h/statbtn.h"     // UpgradeAllowed
#include "../../gen/globals117.h"
#include "../../h/saveload.h"   // get_max_upgrade_level, get_upgrade_level
#include "../platform/port_globals117.h"  // oc_dwReplayFrame

namespace {

bool IsWorker(int nType) { return nType == 0x07 || nType == 0x29 || nType == 0x40; }
bool IsTownHall(int nType) {
  return nType == 0x6A || nType == 0x83 || nType == 0x84 || nType == 0x85 || nType == 0x9A;
}
bool IsMineralField(int nType) { return nType >= 0xB0 && nType <= 0xB2; }
bool IsGeyser(int nType) { return nType == 0xBC; }
bool IsRefinery(int nType) { return nType == 0x6E || nType == 0x95 || nType == 0x9D; }

// A setting of the newbie tab, read once a beat.
bool Flag(const char *pszName)
{
  double d = 0.0;
  PortSettingNumber("hyperion", pszName, &d);
  return d != 0.0;
}

// One order to one unit, without taking the selection over (SelectSmartCaster's way).
void OrderOne(CUnit *pUnit, void (*pfn)(CUnit *))
{
  if (pUnit == NULL)
    return;
  SelectOnlySet(pUnit);
  pfn(pUnit);
  SelectOnlySet(NULL);
}

void TrainWorker(CUnit *pHall)
{
  const WORD wWorker = pHall->GetRace() == 0 ? 0x29 : pHall->GetRace() == 1 ? 0x07 : 0x40;
  SelectOnlySet(pHall);
  send_net_cmd_Build(wWorker);
  // the override stays through the command's execution (PortNewbieFrame clears it after)
}

void Harvest(CUnit *pWorker, CUnit *pPatch)
{
  if (pWorker == NULL || pPatch == NULL || pPatch->pSprite == NULL)
    return;
  SelectOnlySet(pWorker);
  send_net_cmd_RightClick(static_cast<int>(pPatch->pSprite->wPosX), static_cast<int>(pPatch->pSprite->wPosY), pPatch, 0, 0);
  // the override stays through the command's execution
}

void Research(CUnit *pBuilding, int nUpgrade)
{
  if (pBuilding == NULL)
    return;
  SelectOnlySet(pBuilding);
  send_net_cmd_Upgrade(static_cast<upgrade_id_t>(nUpgrade));
  // the override stays through the command's execution
}

// The bases and their patches, for the mining balance: a hall and the patches within
// eight tiles of it, with the workers on each now.
struct Patch {
  CUnit *pUnit;
  int nBase;
  int nOn;
};

bool AutoWorkersOne(int nPlayer)
{
  // the supply, in the player's own race's count: a full one stops the training
  int nRace = 0;
  for (CUnit *p = CUnit::GetUsedList(); p != NULL; p = p->pNext)
    if (p->bPlayer == nPlayer && p->pSprite != NULL && (p->dwFlags & 1) != 0 && IsTownHall(p->wType)) {
      nRace = p->GetRace();
      break;
    }
  const int nUsed = count_get_supplies_used(static_cast<BYTE>(nRace), static_cast<BYTE>(nPlayer));
  const int nMax = count_get_supplies_available(static_cast<BYTE>(nRace), static_cast<BYTE>(nPlayer));
  if (nMax > 0 && nUsed >= nMax)
    return false;
  for (CUnit *p = CUnit::GetUsedList(); p != NULL; p = p->pNext) {
    if (p->bPlayer != nPlayer || p->pSprite == NULL || (p->dwFlags & 1) == 0 || !IsTownHall(p->wType))
      continue;
    if (p->wBuildQueue[p->bBuildQueueSlot] != 0xE4) {
      continue;  // already training
    }
    TrainWorker(p);
    return true;  // one a beat is enough
  }
  return false;
}

bool AutoMineOne(int nPlayer)
{
  // the player's completed town halls and their patches within eight tiles
  std::vector<CUnit *> abase;
  for (CUnit *p = CUnit::GetUsedList(); p != NULL; p = p->pNext)
    if (p->bPlayer == nPlayer && p->pSprite != NULL && (p->dwFlags & 1) != 0 && IsTownHall(p->wType))
      abase.push_back(p);
  std::vector<Patch> ap;
  for (CUnit *p = CUnit::GetUsedList(); p != NULL; p = p->pNext) {
    const bool bRefinery = IsRefinery(p->wType);
    if (!IsMineralField(p->wType) && !IsGeyser(p->wType) && !(bRefinery && p->bPlayer == nPlayer))
      continue;
    if (p->pSprite == NULL)
      continue;
    const int x = static_cast<int>(p->pSprite->wPosX);
    const int y = static_cast<int>(p->pSprite->wPosY);
    int nBest = -1;
    long long llBest = -1;
    for (size_t b = 0; b < abase.size(); ++b) {
      const long long dx = x - static_cast<int>(abase[b]->pSprite->wPosX);
      const long long dy = y - static_cast<int>(abase[b]->pSprite->wPosY);
      const long long d = dx * dx + dy * dy;
      if (d > static_cast<long long>(8 * 32) * (8 * 32))
        continue;
      if (llBest < 0 || d < llBest) {
        llBest = d;
        nBest = static_cast<int>(b);
      }
    }
    if (nBest < 0)
      continue;
    Patch patch;
    patch.pUnit = p;
    patch.nBase = nBest;
    patch.nOn = 0;
    ap.push_back(patch);
  }
  // the workers on each patch (a refinery's target is the refinery itself)
  std::vector<CUnit *> idle;
  std::vector<CUnit *> moved;
  for (CUnit *p = CUnit::GetUsedList(); p != NULL; p = p->pNext) {
    if (p->bPlayer != nPlayer || !IsWorker(p->wType) || (p->dwFlags & 1) == 0)
      continue;
    if (p->bOrderId == 0x02 || p->bOrderId == 0x03)
      idle.push_back(p);
    if (p->pOrderTarget == NULL)
      continue;
    for (Patch &q : ap)
      if (p->pOrderTarget == q.pUnit)
        ++q.nOn;
  }
  // each patch takes up to three workers: one idle worker goes to the first patch with
  // room, and one worker per beat after that, so every base fills and the geysers too
  for (Patch &q : ap) {
    if (q.nOn >= 3 || idle.empty())
      continue;
    CUnit *pWorker = idle.back();
    idle.pop_back();
    if (pWorker != NULL) {
      Harvest(pWorker, q.pUnit);
      return true;
    }
  }
  // a base that is full sends one of its surplus to the next base with room
  for (Patch &q : ap) {
    if (q.nOn <= 3)
      continue;
    for (Patch &r : ap) {
      if (r.nOn >= 3)
        continue;
      for (CUnit *p = CUnit::GetUsedList(); p != NULL; p = p->pNext) {
        if (p->bPlayer != nPlayer || !IsWorker(p->wType) || p->pOrderTarget != q.pUnit)
          continue;
        Harvest(p, r.pUnit);
        return true;
      }
    }
  }
  return false;
}

bool AutoResearchOne(int nPlayer)
{
  for (int u = 0; u < 61; ++u) {
    char szKey[24];
    snprintf(szKey, sizeof szKey, "autoUpg%d", u);
    if (!Flag(szKey))
      continue;
    // already at its top level? nothing to do
    const BYTE bMax = get_max_upgrade_level(static_cast<BYTE>(nPlayer), static_cast<WORD>(u));
    if (bMax == 0)
      continue;
    if (get_upgrade_level(static_cast<BYTE>(nPlayer), static_cast<WORD>(u)) >= bMax)
      continue;
    for (CUnit *p = CUnit::GetUsedList(); p != NULL; p = p->pNext) {
      if (p->bPlayer != nPlayer || p->pSprite == NULL || (p->dwFlags & 1) == 0)
        continue;
      if (p->bOrderId != 0x02 && p->bOrderId != 0x03)
        continue;  // busy, researching or building
      if (UpgradeAllowed(u, nPlayer, p) != 1)
        continue;
      Research(p, u);
      return true;  // one a beat
    }
  }
  return false;
}

}  // namespace

void PortNewbieFrame(void)
{
  if (gPlayback != 0 || gwGameMode != 3)
    return;
  static unsigned int s_nBeat = 0, s_nClearAt = 0;
  const unsigned int nFrame = oc_dwReplayFrame;
  // the beat's order has been carried out by the next frame after the beat, so the
  // selection is the player's own again; the override only ever spans the command's trip
  if (s_nClearAt != 0 && nFrame >= s_nClearAt) {
    SelectOnlySet(NULL);
    s_nClearAt = 0;
  }
  if (nFrame < s_nBeat) {
    // a new game begins: no override from the old game carries into it (the critic's leak)
    s_nBeat = 0;
    s_nClearAt = 0;
    SelectOnlySet(NULL);
  }
  if (s_nBeat != 0 && nFrame < s_nBeat + 12)
    return;
  s_nBeat = nFrame;
  const int nPlayer = static_cast<int>(g_LocalNationID);
  if (nPlayer < 0 || nPlayer >= 12)
    return;
  if (MutatorsOfGame().nNewbie == 0)
    return;
  // one action a beat, with its own override at execution: a batch would all take the last
  // override, so the helpers go one at a time (the critic's finding)
  if (MutatorsOfGame().nAutoWorkers != 0 && AutoWorkersOne(nPlayer)) {
  } else if (MutatorsOfGame().nAutoMine != 0 && AutoMineOne(nPlayer)) {
  } else {
    AutoResearchOne(nPlayer);
  }
  s_nClearAt = nFrame + 2;
}
