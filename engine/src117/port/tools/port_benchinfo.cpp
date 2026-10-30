// port_benchinfo.cpp -- what a benchmark run's overlay reads of the game: the selected units
// in detail and each player's figures (PRD 2026-10-17). Presentation only: it reads,
// never writes game state.
//
// A run is a replay with 1.17's console hidden, so the selection is the overlay's own list of
// unit table indices, kept here. A unit drops out when it dies; the list and the per-slot
// figures are forgotten when a new game starts, which the frame counter going backwards says.
// The figures are 1.17's own: maxima from units.dat, progress as the console's panels compute
// it (lang/statdraw.cpp), supply halved as the console halves it, and the bank and what each
// player gathered from the player tables SyncSlotRead reads (port_synchash.cpp).
#include <windows.h>
#include <string.h>
#include <algorithm>
#include <vector>
#include "port_benchinfo.h"
#include "CUnit.h"
#include "CSprite.h"       // a larva's sprite flags, as the hatchery's own count tests them
#include "CUnitMisc.h"     // get_unit_name_from_type
#include "CUnitUpgrade.h"  // get_upgrade_time
#include "count.h"         // count_get_supplies_used, count_get_supplies_available
#include "order.h"        // status_get_str
#include "saveload.h"      // get_upgrade_level
#include "net_cmds.h"      // PlayerData
#include "typelimits.h"    // UnitTypeValid, and a mod's types with it
#include "../../gen/globals117.h"
#include "../platform/port_globals117.h"
#include "../../h/handicap.h"  // HandicapShields

// Three tables gen's globals117.h does not carry, named here as the TUs that read them do:
// units.dat's armour, weapons.dat's damage upgrade per weapon, and what each player gathered.
extern BYTE gubUnitArmor_g[228] OC_UNALIGNED;  // 0x521D90, units.dat's armour by unit type
extern upgrade_id_t wpn_dmgUpg_g[];            // 0x5256C0, the upgrade id of each weapon

extern WORD wpn_label_g[];  // 0x524E10, weapons.dat's label: the name's string

namespace {

enum {
  TYPE_NONE = UNIT_TYPES_117,      // 228: an empty build queue slot (CUnit::IsConstructing)
  UNIT_COMPLETED = 1,              // CUnit::dwFlags bit 0, the bit CUnit::Complete sets
  TYPE_BUILDING = 1,               // gulUnitTypeFlags bit 0
  TYPE_WORKER = 8,                 // gulUnitTypeFlags bit 3 (CUnit::TransportHasWorker)
  TYPE_FLYER = 4,                  // gulUnitTypeFlags bit 2
  TYPE_RESOURCES = 0x2000,         // gulUnitTypeFlags bit 13, a resource container
  SPRITE_HIDDEN = 0x20,            // CSprite::flags bit 5, which the larva count skips
  TYPE_LARVA = 0x23,
  TYPE_EGG = 0x24,
  TYPE_COCOON = 0x3B,
  TYPE_LURKER_EGG = 0x61,
  TYPE_HATCHERY = 0x83,
  TYPE_LAIR = 0x84,
  TYPE_HIVE = 0x85
};

int g_anSelect[PORT_BENCH_SELECT_MAX];
int g_nSelect;
bool g_bOwn;  // set by the live channel; else the game's own selection (gpStatUnits)
// The frame each slot was first seen out of the game, and whether it was ever seen in it: a
// slot with no units at a game's first frames has not gone out, it has not started yet.
unsigned g_adwOutFrame[PLAYER_SLOTS_MAX];
bool g_abWasIn[PLAYER_SLOTS_MAX];  // a seat this game: a person's or a computer's
// The frame each slot was last seen with nothing left, forgotten as soon as it owns a unit
// again: a game's loser never does, but the canvas and clear leave a slot empty for a while.
unsigned g_adwEmptyFrame[PLAYER_SLOTS_MAX];
unsigned g_dwLastFrame;

// A frame before the last one read is a new game: forget the selection and the slots.
void NewGameCheck()
{
  if (oc_dwReplayFrame < g_dwLastFrame) {
    memset(g_adwOutFrame, 0, sizeof g_adwOutFrame);
    memset(g_adwEmptyFrame, 0, sizeof g_adwEmptyFrame);
    memset(g_abWasIn, 0, sizeof g_abWasIn);
    g_nSelect = 0;
    g_bOwn = false;
  }
  g_dwLastFrame = oc_dwReplayFrame;
}

// The unit table's used list, sorted, so an index can be tested for being alive the way
// port_control.cpp's Units does.
void UsedList(std::vector<CUnit *> *pOut)
{
  pOut->clear();
  for (CUnit *pUnit = CUnit::GetUsedList(); pUnit != NULL; pUnit = pUnit->pNext)
    pOut->push_back(pUnit);
  std::sort(pOut->begin(), pOut->end());
}

// The unit at an index of the table while it is alive: in the used list, and with hit points
// left or a building still going up.
CUnit *UnitAt(const std::vector<CUnit *> &used, int nIndex)
{
  if (nIndex < 0 || nIndex >= POOL_UNITS_STORE)
    return NULL;
  CUnit *pUnit = CUnit::Head() + nIndex;
  if (!std::binary_search(used.begin(), used.end(), pUnit))
    return NULL;
  const bool bGoingUp = UnitTypeValid(pUnit->wType) &&
                        (gulUnitTypeFlags[pUnit->wType] & TYPE_BUILDING) != 0 &&
                        !(pUnit->dwFlags & UNIT_COMPLETED);
  return pUnit->lHitPoints > 0 || bGoingUp ? pUnit : NULL;
}

// The build time of a type, 0 when there is none to divide by.
int TimeToBuild(unsigned nType)
{
  return UnitTypeValid(nType) ? guwUnitTimeToBuild[nType] : 0;
}

// What a unit is doing, 0 to 100, or -1 for nothing. The unit itself going up and a morph
// both go through CUnit::GetBuildPercentage, which takes the queue's type for a morph; a
// research and an upgrade count their own time down against their time cost; and a building
// making something reads the percentage of the unit it is making, as the console's training
// panel does. Each divisor is checked, which the console does not have to do.
int Progress(CUnit *pUnit)
{
  const WORD wType = pUnit->wType;
  const bool bMorph = wType == TYPE_EGG || wType == TYPE_COCOON || wType == TYPE_LURKER_EGG ||
                      pUnit->IsZergBldgUpgrading();
  if (!(pUnit->dwFlags & UNIT_COMPLETED) || bMorph) {
    const WORD wBuilt = bMorph ? pUnit->wBuildQueue[pUnit->bBuildQueueSlot] : wType;
    return TimeToBuild(wBuilt) != 0 ? pUnit->GetBuildPercentage() : -1;
  }
  // past here the fields are a building's: a unit's same bytes hold other things (a worker's
  // harvest, the building it is to place), and the console shows it nothing in progress
  if (!UnitTypeValid(wType) || !(gulUnitTypeFlags[wType] & TYPE_BUILDING))
    return -1;
  if (pUnit->bTechType != TECH_NONE) {
    const int nTotal = techdata_timeCost_g[pUnit->bTechType];
    return nTotal > 0 ? (nTotal - pUnit->wRemainingResearchTime) * 100 / nTotal : -1;
  }
  if (pUnit->bUpgradeType != UPGRADE_NONE) {
    const int nTotal = get_upgrade_time(pUnit->bPlayer, pUnit->bUpgradeType);
    return nTotal > 0 ? (nTotal - pUnit->wRemainingResearchTime) * 100 / nTotal : -1;
  }
  CUnit *pMaking = pUnit->GetConstructionTarget();
  if (pMaking != NULL && TimeToBuild(pMaking->wType) != 0)
    return pMaking->GetBuildPercentage();
  return -1;
}

// A hatchery's, lair's or hive's larvae, counted as the game counts them for the next one
// (CUnit::DispatchSpawningLarva through FindCountLarvae): a larva whose parent link is this
// building and whose sprite is not hidden.
int Larvae(const std::vector<CUnit *> &used, const CUnit *pTown)
{
  const WORD wType = pTown->wType;
  if (wType != TYPE_HATCHERY && wType != TYPE_LAIR && wType != TYPE_HIVE)
    return 0;
  int nCount = 0;
  for (size_t i = 0; i < used.size(); ++i) {
    const CUnit *p = used[i];
    if (p->wType == TYPE_LARVA && p->pUnion80 == pTown && p->pSprite != NULL &&
        !(p->pSprite->flags.b & SPRITE_HIDDEN))
      ++nCount;
  }
  return nCount;
}

void FillUnit(CUnit *pUnit, const std::vector<CUnit *> &used, PortBenchUnit *pOut)
{
  memset(pOut, 0, sizeof *pOut);
  const unsigned nType = pUnit->wType;
  const bool bKnown = UnitTypeValid(nType);
  pOut->nIndex = static_cast<int>(pUnit - CUnit::Head());
  pOut->nType = static_cast<int>(nType);
  pOut->nPlayer = pUnit->bPlayer;
  const char *pszName = get_unit_name_from_type(static_cast<WORD>(nType));
  if (pszName != NULL)
    strncpy(pOut->szName, pszName, sizeof pOut->szName - 1);

  // Hit points as the console's own line reads them (statdraw_draw_hp): rounded up, so a
  // unit with any fraction of a point left is on 1, against CUnit::GetLife's maximum.
  pOut->nHp = static_cast<int>(pUnit->lHitPoints + 0xFF) >> 8;
  pOut->nHpMax = pUnit->GetLife();
  if (bKnown && gbUnitHasShields[nType]) {
    // guwUnitEnergy (the ledger's name) is units.dat's shield amount in whole points, as the
    // console reads it, for the type the console shows: a morphing zerg building's next one
    const unsigned nShown = pUnit->GetTypeEx();
    pOut->nShields = static_cast<LONG>(pUnit->lShields) >> 8;
    pOut->nShieldsMax = UnitTypeValid(nShown) ? HandicapShields(guwUnitEnergy[nShown], pUnit->bPlayer) : 0;  //
  }
  if (pUnit->HasEnergy()) {
    pOut->nEnergy = pUnit->wEnergy >> 8;
    pOut->nEnergyMax = pUnit->GetMaxEnergyReal();
  }
  pOut->nKills = pUnit->bKillCount;

  // The weapons as the console shows them: a type with none of its own takes its subunit's,
  // which is what CUnit::GetWeaponGnd and GetWeaponAir give back.
  const weapon_id_t bGnd = pUnit->GetWeaponGnd();
  const weapon_id_t bAir = pUnit->GetWeaponAir();
  weapon_id_t bWeapon = bGnd != WEAPON_NONE ? bGnd : bAir;
  // a lurker has its weapon only burrowed (GetWeaponGnd); the console shows units.dat's
  if (bWeapon == WEAPON_NONE && bKnown)
    bWeapon = gubUnitWeaponGnd[nType] != WEAPON_NONE ? gubUnitWeaponGnd[nType] : gubUnitWeaponAir[nType];
  pOut->bHasWeapon = bWeapon != WEAPON_NONE;
  if (pOut->bHasWeapon) {
    // the console's weapon tip: the name, the base damage and the upgrades' bonus (statdata_ctx_status)
    lstrcpynA(pOut->szWeapon, status_get_str(wpn_label_g[bWeapon]), sizeof pOut->szWeapon);
    pOut->nDamage = pUnit->GetDisplayBaseDamage(bWeapon);
    pOut->nDamageUp = pUnit->GetDisplayBonusDamage(bWeapon);
  }
  pOut->nWeaponUp = pOut->bHasWeapon ? get_upgrade_level(pUnit->bPlayer, wpn_dmgUpg_g[bWeapon]) : -1;
  pOut->nArmour = bKnown ? gubUnitArmor_g[nType] : 0;
  pOut->nArmourUp = pUnit->ArmorUpgrades();

  pOut->bBuilding = bKnown && (gulUnitTypeFlags[nType] & TYPE_BUILDING) != 0;
  pOut->bWorker = bKnown && (gulUnitTypeFlags[nType] & TYPE_WORKER) != 0;
  pOut->bAir = bKnown && (gulUnitTypeFlags[nType] & TYPE_FLYER) != 0;
  pOut->nResources = bKnown && (gulUnitTypeFlags[nType] & TYPE_RESOURCES) != 0 ? pUnit->wResourceAmount : -1;
  pOut->bTransport = pUnit->IsATransport() != FALSE;

  // the queue as the console shows it, a building's or a morphing unit's; a worker's slot holds
  // the building it is going to place, which the console does not show
  const bool bMorphing = nType == TYPE_EGG || nType == TYPE_COCOON || nType == TYPE_LURKER_EGG;
  for (int i = 0; i < 5; ++i) {
    pOut->anQueue[i] = pOut->bBuilding || bMorphing ? pUnit->GetBuildUnit(i) : TYPE_NONE;
    if (pOut->anQueue[i] != TYPE_NONE && pOut->nQueue == i)
      pOut->nQueue = i + 1;
  }
  pOut->nProgressPercent = Progress(pUnit);
  pOut->nLarvae = Larvae(used, pUnit);
  // The cargo slots hold unit ids, and only a type with a hold uses them (CUnit::IsUnitInside
  // walks the same eight), so a type without one is left with no cargo rather than read.
  if (bKnown && gubUnitCargoHold[nType] != 0) {
    pOut->nCargoHold = gubUnitCargoHold[nType];
    for (int i = 0; i < 8; ++i) {
      CUnit *pLoaded = CUnit::FromIdAlive(pUnit->wCargoUnitIds[i]);
      if (pLoaded != NULL) {
        pOut->anCargo[pOut->nCargo] = pLoaded->wType;
        pOut->anCargoSpace[pOut->nCargo++] = gubUnitVolume[pLoaded->wType];
        pOut->nCargoUsed += gubUnitVolume[pLoaded->wType];
      }
    }
  }
}

// The selection with the units that have died taken out of it.
void SelectPrune(const std::vector<CUnit *> &used)
{
  // the game's selection, which a click, a box and Shift make in a replay as in 1.17
  if (!g_bOwn) {
    g_nSelect = 0;
    for (int i = 0; i < PORT_BENCH_SELECT_MAX; ++i)
      if (StatUnits()[i] != NULL)
        g_anSelect[g_nSelect++] = static_cast<int>(StatUnits()[i] - CUnit::Head());
  }
  int nKept = 0;
  for (int i = 0; i < g_nSelect; ++i)
    if (UnitAt(used, g_anSelect[i]) != NULL)
      g_anSelect[nKept++] = g_anSelect[i];
  g_nSelect = nKept;
}

}  // namespace

void PortBenchSelectSet(const int *pnIndices, int nCount)
{
  NewGameCheck();
  g_bOwn = nCount > 0;
  g_nSelect = 0;
  for (int i = 0; i < nCount && g_nSelect < PORT_BENCH_SELECT_MAX; ++i) {
    if (pnIndices[i] < 0)
      continue;
    bool bHave = false;
    for (int k = 0; k < g_nSelect; ++k)
      bHave = bHave || g_anSelect[k] == pnIndices[i];
    if (!bHave)
      g_anSelect[g_nSelect++] = pnIndices[i];
  }
}

void PortBenchSelectionClear(void)
{
  g_bOwn = false;
  g_nSelect = 0;
}

int PortBenchSelection(int *pnIndices, int nMax)
{
  NewGameCheck();
  std::vector<CUnit *> used;
  UsedList(&used);
  SelectPrune(used);
  const int nCount = (std::min)(g_nSelect, nMax);
  for (int i = 0; i < nCount; ++i)
    pnIndices[i] = g_anSelect[i];
  return nCount;
}

int PortBenchSelected(PortBenchUnit *pOut, int nMax)
{
  NewGameCheck();
  std::vector<CUnit *> used;
  UsedList(&used);
  SelectPrune(used);
  int nCount = 0;
  for (int i = 0; i < g_nSelect && nCount < nMax; ++i) {
    CUnit *pUnit = UnitAt(used, g_anSelect[i]);
    if (pUnit != NULL)
      FillUnit(pUnit, used, &pOut[nCount++]);
  }
  return nCount;
}

bool PortBenchUnitRead(int nIndex, PortBenchUnit *pOut)
{
  std::vector<CUnit *> used;
  UsedList(&used);
  CUnit *pUnit = UnitAt(used, nIndex);
  if (pUnit == NULL)
    return false;
  FillUnit(pUnit, used, pOut);
  return true;
}

// A seat is a person's or a computer's (types 1 and 2). One that leaves or is beaten becomes 8
// (sub_478C50) and 11 once it has nothing left, so the seats are noted each frame of a run, and
// the frame one stops being a seat is its out frame.
void PortBenchPlayersNote(void)
{
  NewGameCheck();
  for (int s = 0; s < PlayerSlots(); ++s) {
    const bool bSeat = gPlayerData[s].nType == 1 || gPlayerData[s].nType == 2;
    if (bSeat)
      g_abWasIn[s] = true;
    else if (g_abWasIn[s] && g_adwOutFrame[s] == 0)
      g_adwOutFrame[s] = oc_dwReplayFrame;
  }
}

bool PortBenchWasSeat(int nSlot)
{
  return nSlot >= 0 && nSlot < PLAYER_SLOTS_MAX && g_abWasIn[nSlot];
}

int PortBenchPlayersRead(PortBenchPlayer *pOut, int nMax)
{
  PortBenchPlayersNote();
  int anUnits[PLAYER_SLOTS_MAX] = {};
  for (CUnit *pUnit = CUnit::GetUsedList(); pUnit != NULL; pUnit = pUnit->pNext)
    if (pUnit->bPlayer < PLAYER_SLOTS_MAX)
      ++anUnits[pUnit->bPlayer];
  int nCount = 0;
  for (int s = 0; s < PlayerSlots() && nCount < nMax; ++s) {
    // a seat this game, still in or gone (PortBenchPlayersNote)
    if (!g_abWasIn[s])
      continue;
    PortBenchPlayer *p = &pOut[nCount++];
    memset(p, 0, sizeof *p);
    p->nSlot = s;
    p->nRace = gPlayerData[s].nRace;
    p->nTeam = gPlayerData[s].nTeam;
    p->nMinerals = gPlayerMinerals[s];
    p->nGas = gPlayerGas[s];
    p->nMineralsGathered = gPlayerMineralsGathered_g[s];
    p->nGasGathered = gPlayerGasGathered_g[s];
    // Supply is kept in halves, a race at a time; only the three races have any.
    if (p->nRace >= 0 && p->nRace <= 2) {
      const BYTE bRace = static_cast<BYTE>(p->nRace), bSlot = static_cast<BYTE>(s);
      p->nSupplyUsed = (count_get_supplies_used(bRace, bSlot) + 1) / 2;
      p->nSupplyMax = (count_get_supplies_available(bRace, bSlot) + 1) / 2;
    }
    p->nUnits = anUnits[s];
    // in while it owns a unit and the game has not marked it gone; owning nothing is read each
    // time, not kept, so a slot the canvas or clear emptied is in again once it owns a unit
    p->bLeft = anUnits[s] != 0 && g_adwOutFrame[s] == 0;
    if (anUnits[s] != 0)
      g_adwEmptyFrame[s] = 0;
    else if (g_adwEmptyFrame[s] == 0)
      g_adwEmptyFrame[s] = oc_dwReplayFrame;
    p->nOutFrame = p->bLeft ? 0 : g_adwOutFrame[s] != 0 ? g_adwOutFrame[s] : g_adwEmptyFrame[s];
  }
  return nCount;
}

int PortBenchSelectedCount(void)
{
  if (g_bOwn)
    return g_nSelect;
  int n = 0;
  for (int i = 0; i < PORT_BENCH_SELECT_MAX; ++i)
    n += StatUnits()[i] != NULL;
  return n;
}
