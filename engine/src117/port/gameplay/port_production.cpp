// port_production.cpp -- a left-hand research browser and a global production queue.
//
// Research is a Newbie-mode aid: every currently allowed research and upgrade from every completed
// local building, not only the selection. Production is independent of the mutator and lists every
// local build queue (including its waiting slots), research, upgrade, morph and building going up.
// Rows use Remastered's command icons through the console's HD item list. Ui* draws the panels,
// words and progress; a small text badge is the classic-art/no-capacity fallback.
//
// Only a live player's click acts. Research and upgrades are aimed at one free building with
// SelectOnlySet and the same net commands as their command-card buttons. The override is kept for
// two game frames, as port_newbie.cpp's commands do, so the queued net command executes against
// that building. Source clicks send Select and centre; in a replay or an observed game they only
// centre the viewer. No row ever sends a cancel command.
#include <windows.h>
#include <SDL3/SDL.h>
#include <algorithm>
#include <stdio.h>
#include <string.h>
#include <string>
#include <vector>
#include "port_production.h"
#include "port_keys.h"
#include "port_mutators.h"
#include "port_watchgame.h"
#include "CUnit.h"
#include "CSprite.h"
#include "CUnitMisc.h"
#include "CUnitUpgrade.h"
#include "net_cmds.h"
#include "order.h"
#include "playerlimits.h"
#include "saveload.h"
#include "statbtn.h"
#include "typelimits.h"
#include "oc_data.h"
#include "../data/port_moddata.h"
#include "../front/port_ui.h"
#include "../lab/port_labwatch.h"
#include "../lan/port_lanfeed.h"
#include "../platform/port_globals117.h"
#include "../render/port_gpu_hd.h"
#include "../render/port_hd.h"
#include "../render/port_modart.h"
#include "../render/port_overlay.h"
#include "../render/port_view.h"
#include "../../gen/globals117.h"

#ifdef OC_DLL_MODE
#define tech_icon (reinterpret_cast<WORD *>(OC_VA(0x51F970u)))
#define upg_icon (reinterpret_cast<WORD *>(OC_VA(0x524C50u)))
#else
extern WORD tech_icon[44];
extern WORD upg_icon[];
#endif

namespace {

enum Panel { PANEL_RESEARCH, PANEL_QUEUE, PANELS };
enum RowKind { ROW_RESEARCH, ROW_UPGRADE, ROW_TRAIN, ROW_MORPH, ROW_CONSTRUCT };
enum HitKind { HIT_PANEL, HIT_ROW, HIT_CLOSE, HIT_UP, HIT_DOWN };

const int kWidth = 292;
const int kRowH = 36;
const int kHeaderH = 42;
const int kFooterH = 20;
const int kMaxRows = 9;
const int kIcon = 32;

struct Row {
  RowKind kind = ROW_TRAIN;
  unsigned id = 0;         // tech, upgrade or unit type
  DWORD dwSource = 0;      // the building/source's generation-safe unit id
  int nQueue = 0;          // build queue position; 0 is active
  int nProgress = -1;      // -1: waiting or not exposed
  int nLevel = 0;
  int nTargetLevel = 0;
  int nMaxLevel = 0;
  int nIcon = 0;
  int nSources = 1;
  bool bActive = false;
  bool bHdItem = false;
  std::string name;
  std::string context;
};

struct Box { int x = 0, y = 0, w = 0, h = 0; };
struct FBox { float x = 0, y = 0, w = 0, h = 0; };
struct LayoutRow { int nRow = -1; Box icon; };
struct PanelLayout {
  bool bShown = false;
  Box box;
  int nFirst = 0;
  int nVisible = 0;
  int nCapacity = 0;
  LayoutRow rows[kMaxRows];
  FBox target;  // the whole panel in window pixels, from the last draw
};
struct RowKey {
  RowKind kind = ROW_TRAIN;
  unsigned id = 0;
  DWORD dwSource = 0;
  int nQueue = 0;
};
struct Hit {
  FBox box;
  HitKind kind = HIT_PANEL;
  int nPanel = 0;
  RowKey key;
};

bool g_bResearch;
bool g_bQueue;
bool g_bInGame;
unsigned g_dwLastFrame;
unsigned g_dwRowsFrame = ~0u;
bool g_bRowsValid;
bool g_bLayoutValid;
std::vector<Row> g_rows[PANELS];
PanelLayout g_layout[PANELS];
int g_anScroll[PANELS];
std::vector<Hit> g_hits;
bool g_bPressed;
Hit g_pressed;
float g_fMouseX, g_fMouseY;
bool g_bOwnOverride;
unsigned g_dwOverrideFrame;
unsigned g_dwOverrideClear;

const char *const kTechNames[TECH_TYPES_117] = {
    "Stim Packs", "Lockdown", "EMP Shockwave", "Spider Mines", "Scanner Sweep", "Tank Siege Mode",
    "Defensive Matrix", "Irradiate", "Yamato Gun", "Cloaking Field", "Personnel Cloaking", "Burrowing",
    "Infestation", "Spawn Broodlings", "Dark Swarm", "Plague", "Consume", "Ensnare", "Parasite",
    "Psionic Storm", "Hallucination", "Recall", "Stasis Field", "Archon Warp", "Restoration",
    "Disruption Web", "Unused Tech 26", "Mind Control", "Dark Archon Meld", "Feedback", "Optical Flare",
    "Maelstrom", "Lurker Aspect", "Unused Tech 33", "Healing", "Unused Tech 35", "Unused Tech 36",
    "Unused Tech 37", "Unused Tech 38", "Unused Tech 39", "Unused Tech 40", "Unused Tech 41",
    "Unused Tech 42", "Unused Tech 43"};
const char *const kUpgradeNames[UPGRADE_TYPES_117] = {
    "Terran Infantry Armor", "Terran Vehicle Plating", "Terran Ship Plating", "Zerg Carapace",
    "Zerg Flyer Carapace", "Protoss Ground Armor", "Protoss Air Armor", "Terran Infantry Weapons",
    "Terran Vehicle Weapons", "Terran Ship Weapons", "Zerg Melee Attacks", "Zerg Missile Attacks",
    "Zerg Flyer Attacks", "Protoss Ground Weapons", "Protoss Air Weapons", "Protoss Plasma Shields",
    "U-238 Shells", "Ion Thrusters", "Burst Lasers", "Titan Reactor", "Ocular Implants", "Moebius Reactor",
    "Apollo Reactor", "Colossus Reactor", "Ventral Sacs", "Antennae", "Pneumatized Carapace",
    "Metabolic Boost", "Adrenal Glands", "Muscular Augments", "Grooved Spines", "Gamete Meiosis",
    "Metasynaptic Node", "Singularity Charge", "Leg Enhancements", "Scarab Damage", "Reaver Capacity",
    "Gravitic Drive", "Sensor Array", "Gravitic Boosters", "Khaydarin Amulet", "Apial Sensors",
    "Gravitic Thrusters", "Carrier Capacity", "Khaydarin Core", "Unused Upgrade 45", "Unused Upgrade 46",
    "Argus Jewel", "Unused Upgrade 48", "Argus Talisman", "Unused Upgrade 50", "Caduceus Reactor",
    "Chitinous Plating", "Anabolic Synthesis", "Charon Boosters", "Unused Upgrade 55", "Unused Upgrade 56",
    "Unused Upgrade 57", "Unused Upgrade 58", "Unused Upgrade 59", "Unused Upgrade 60"};

int LocalPlayer(void)
{
  const int n = static_cast<int>(g_LocalNationID);
  return n >= 0 && n < PLAYER_SLOTS_MAX ? n : -1;
}

bool Observer(void)
{
  return gPlayback != 0 || PortLanWatchLive() || PortLabWatchLive() || PortWatchLive() ||
         g_ActiveNationID != g_LocalNationID;
}

bool BuildingType(unsigned t)
{
  return UnitTypeValid(t) && (gulUnitTypeFlags[t] & 1) != 0;
}

bool EggType(unsigned t)
{
  return t == 0x24 || t == 0x3B || t == 0x61;
}

bool QueueSource(const CUnit *p)
{
  if (BuildingType(p->wType) || p->wType == 0x23)  // buildings and larvae
    return true;
  return p->wType == 0x48 || p->wType == 0x51 || p->wType == 0x52 || p->wType == 0x53;  // hangars
}

bool ResearchFree(const CUnit *p)
{
  return (p->dwFlags & 1) != 0 && (p->bOrderId == 0x02 || p->bOrderId == 0x03) &&
         p->bTechType == TECH_NONE && p->bUpgradeType == UPGRADE_NONE;
}

int ClampPercent(int n)
{
  return SDL_clamp(n, 0, 100);
}

int BuildPercent(CUnit *p, unsigned t)
{
  if (p == NULL || !UnitTypeValid(t) || guwUnitTimeToBuild[t] == 0)
    return -1;
  return ClampPercent(p->GetBuildPercentage());
}

int ResearchPercent(CUnit *p)
{
  int nTotal = 0;
  // The 1.17 icon/time tables have only their original rows. Added research still shows its
  // name and source; without public mod-time accessors, do not read past either base table.
  if (p->bTechType != TECH_NONE && TechTypeValid(p->bTechType) && !TechTypeMod(p->bTechType))
    nTotal = techdata_timeCost_g[p->bTechType];
  else if (p->bUpgradeType != UPGRADE_NONE && UpgradeTypeValid(p->bUpgradeType) &&
           !UpgradeTypeMod(p->bUpgradeType))
    nTotal = get_upgrade_time(p->bPlayer, p->bUpgradeType);
  return nTotal > 0 ? ClampPercent((nTotal - p->wRemainingResearchTime) * 100 / nTotal) : -1;
}

std::string UnitName(unsigned t)
{
  const char *psz = UnitTypeValid(t) ? get_unit_name_from_type(static_cast<WORD>(t)) : NULL;
  if (psz != NULL && psz[0] != 0)
    return psz;
  char sz[32];
  snprintf(sz, sizeof sz, "Unit %u", t);
  return sz;
}

// A mod's added research card supplies the added tech/upgrade's display name.
std::string CardName(bool bUpgrade, unsigned id)
{
  int nButtons = 0;
  const ModCardButton *pButtons = ModCardButtons(&nButtons);
  for (int i = 0; i < nButtons; ++i) {
    const ModCardButton &b = pButtons[i];
    if (b.wTech != id || b.bUpgrade != bUpgrade || (!b.bUpgrade && !b.bResearch))
      continue;
    const char *psz = status_get_str(b.wStr);
    if (psz == NULL)
      break;
    if (psz[0] != 0 && psz[1] != 0 && psz[2] != 0)
      psz += 2;  // the command button's hotkey and cost-kind bytes
    std::string out;
    for (; *psz != 0 && *psz != '\n'; ++psz)
      if (static_cast<unsigned char>(*psz) >= 0x20 && *psz != '\x03' && *psz != '\x01')
        out += *psz;
    if (!out.empty())
      return out;
    break;
  }
  if (bUpgrade && id < UPGRADE_TYPES_117)
    return kUpgradeNames[id];
  if (!bUpgrade && id < TECH_TYPES_117)
    return kTechNames[id];
  char sz[40];
  snprintf(sz, sizeof sz, "%s %u", bUpgrade ? "Upgrade" : "Tech", id);
  return sz;
}

int CardIcon(bool bUpgrade, unsigned id)
{
  const bool bMod = bUpgrade ? id >= UPGRADE_TYPE_MOD_FIRST : id >= TECH_TYPE_MOD_FIRST;
  if (!bMod)
    return bUpgrade ? upg_icon[id] : tech_icon[id];
  int nButtons = 0;
  const ModCardButton *pButtons = ModCardButtons(&nButtons);
  for (int i = 0; i < nButtons; ++i)
    if (pButtons[i].wTech == id && pButtons[i].bUpgrade == bUpgrade &&
        (bUpgrade || pButtons[i].bResearch))
      return pButtons[i].wIcon;
  return -1;  // no added card/icon: the overlay's text badge is the safe fallback
}

std::string Badge(const std::string &name)
{
  std::string out;
  for (const char *p = name.c_str(); *p != 0 && out.size() < 2; ++p)
    if ((*p >= 'A' && *p <= 'Z') || (*p >= 'a' && *p <= 'z') || (*p >= '0' && *p <= '9')) {
      char c = *p;
      if (c >= 'a' && c <= 'z')
        c = static_cast<char>(c - 'a' + 'A');
      out += c;
    }
  return out.empty() ? std::string("?") : out;
}

RowKey KeyOf(const Row &r)
{
  RowKey key;
  key.kind = r.kind;
  key.id = r.id;
  key.dwSource = r.dwSource;
  key.nQueue = r.nQueue;
  return key;
}

bool SameKey(const RowKey &a, const RowKey &b)
{
  return a.kind == b.kind && a.id == b.id && a.dwSource == b.dwSource && a.nQueue == b.nQueue;
}

CUnit *Source(DWORD dwId)
{
  CUnit *p = dwId != 0 ? CUnit::FromIdAlive(dwId) : NULL;
  return p != NULL && p->bPlayer == LocalPlayer() ? p : NULL;
}

int FindRow(int nPanel, RowKind kind, unsigned id)
{
  const std::vector<Row> &rows = g_rows[nPanel];
  for (size_t i = 0; i < rows.size(); ++i)
    if (rows[i].kind == kind && rows[i].id == id)
      return static_cast<int>(i);
  return -1;
}

void AddResearchRow(CUnit *p, bool bUpgrade, unsigned id)
{
  const RowKind kind = bUpgrade ? ROW_UPGRADE : ROW_RESEARCH;
  if (FindRow(PANEL_RESEARCH, kind, id) >= 0)
    return;
  Row r;
  r.kind = kind;
  r.id = id;
  r.dwSource = static_cast<DWORD>(p->GetUnitID());
  r.bActive = true;
  r.nProgress = ResearchPercent(p);
  r.name = CardName(bUpgrade, id);
  r.context = UnitName(p->wType);
  r.nIcon = CardIcon(bUpgrade, id);
  if (bUpgrade) {
    r.nLevel = get_upgrade_level(p->bPlayer, static_cast<WORD>(id));
    r.nMaxLevel = get_max_upgrade_level(p->bPlayer, static_cast<WORD>(id));
    r.nTargetLevel = p->bUpgradeType == id && p->bTargetUpgradeLevel > r.nLevel ? p->bTargetUpgradeLevel : r.nLevel + 1;
  }
  g_rows[PANEL_RESEARCH].push_back(r);
}

void AddAvailableRow(CUnit *p, bool bUpgrade, unsigned id, int nSources)
{
  const RowKind kind = bUpgrade ? ROW_UPGRADE : ROW_RESEARCH;
  if (FindRow(PANEL_RESEARCH, kind, id) >= 0)
    return;
  Row r;
  r.kind = kind;
  r.id = id;
  r.dwSource = static_cast<DWORD>(p->GetUnitID());
  r.name = CardName(bUpgrade, id);
  r.context = UnitName(p->wType);
  r.nSources = nSources;
  r.nIcon = CardIcon(bUpgrade, id);
  if (bUpgrade) {
    r.nLevel = get_upgrade_level(p->bPlayer, static_cast<WORD>(id));
    r.nMaxLevel = get_max_upgrade_level(p->bPlayer, static_cast<WORD>(id));
    r.nTargetLevel = r.nLevel + 1;
  }
  g_rows[PANEL_RESEARCH].push_back(r);
}

void BuildResearchRows(const std::vector<CUnit *> &units, int nPlayer)
{
  std::vector<CUnit *> buildings;
  for (CUnit *p : units) {
    if (!BuildingType(p->wType) || (p->dwFlags & 1) == 0)
      continue;
    buildings.push_back(p);
    if (p->bTechType != TECH_NONE && TechTypeValid(p->bTechType))
      AddResearchRow(p, false, p->bTechType);
    if (p->bUpgradeType != UPGRADE_NONE && UpgradeTypeValid(p->bUpgradeType))
      AddResearchRow(p, true, p->bUpgradeType);
  }
  // Every base tech, then every added mod tech. ResearchAllowed is the command receiver's gate.
  // TechUseAllowed is evaluated too, as tech cards require, but cannot be the research gate: its
  // use tree legitimately hides an unresearched tech and can name the caster rather than the lab.
  for (unsigned pass = 0; pass < 2; ++pass) {
    const unsigned begin = pass == 0 ? 0 : TECH_TYPE_MOD_FIRST;
    const unsigned end = pass == 0 ? TECH_TYPES_117 : static_cast<unsigned>(g_hxTypes.techs);
    for (unsigned t = begin; t < end; ++t) {
      if (FindRow(PANEL_RESEARCH, ROW_RESEARCH, t) >= 0)
        continue;
      CUnit *pFirst = NULL;
      int nSources = 0;
      for (CUnit *p : buildings) {
        if (!ResearchFree(p) || ResearchAllowed(t, nPlayer, p) != 1)
          continue;
        (void)TechUseAllowed(t, nPlayer, p);
        if (pFirst == NULL)
          pFirst = p;
        ++nSources;
      }
      if (pFirst != NULL)
        AddAvailableRow(pFirst, false, t, nSources);
    }
  }
  for (unsigned pass = 0; pass < 2; ++pass) {
    const unsigned begin = pass == 0 ? 0 : UPGRADE_TYPE_MOD_FIRST;
    const unsigned end = pass == 0 ? UPGRADE_TYPES_117 : static_cast<unsigned>(g_hxTypes.upgrades);
    for (unsigned u = begin; u < end; ++u) {
      if (FindRow(PANEL_RESEARCH, ROW_UPGRADE, u) >= 0)
        continue;
      CUnit *pFirst = NULL;
      int nSources = 0;
      for (CUnit *p : buildings) {
        if (!ResearchFree(p) || UpgradeAllowed(u, nPlayer, p) != 1)
          continue;
        if (pFirst == NULL)
          pFirst = p;
        ++nSources;
      }
      if (pFirst != NULL)
        AddAvailableRow(pFirst, true, u, nSources);
    }
  }
}

void AddQueueRow(CUnit *pSource, RowKind kind, unsigned id, int nQueue, int nProgress)
{
  if (!UnitTypeValid(id))
    return;
  Row r;
  r.kind = kind;
  r.id = id;
  r.dwSource = static_cast<DWORD>(pSource->GetUnitID());
  r.nQueue = nQueue;
  r.nProgress = nProgress;
  r.bActive = nQueue == 0;
  r.name = UnitName(id);
  r.context = UnitName(pSource->wType);
  r.nIcon = static_cast<int>(ModUnitArtFrame(MOD_ART_ICON, id));
  g_rows[PANEL_QUEUE].push_back(r);
}

void AddQueueResearch(CUnit *p, bool bUpgrade, unsigned id)
{
  Row r;
  r.kind = bUpgrade ? ROW_UPGRADE : ROW_RESEARCH;
  r.id = id;
  r.dwSource = static_cast<DWORD>(p->GetUnitID());
  r.nProgress = ResearchPercent(p);
  r.bActive = true;
  r.name = CardName(bUpgrade, id);
  r.context = UnitName(p->wType);
  r.nIcon = CardIcon(bUpgrade, id);
  if (bUpgrade) {
    r.nLevel = get_upgrade_level(p->bPlayer, static_cast<WORD>(id));
    r.nMaxLevel = get_max_upgrade_level(p->bPlayer, static_cast<WORD>(id));
    r.nTargetLevel = p->bTargetUpgradeLevel > r.nLevel ? p->bTargetUpgradeLevel : r.nLevel + 1;
  }
  g_rows[PANEL_QUEUE].push_back(r);
}

unsigned MorphTarget(CUnit *p)
{
  if (EggType(p->wType) || p->IsZergBldgUpgrading())
    return p->wBuildQueue[p->bBuildQueueSlot];
  if (p->bOrderId == 0x6A)
    return p->wType == 0x3D || p->wType == 0x4B ? 0x3F : 0x44;  // dark or ordinary archon
  return UNIT_TYPES_117;
}

void BuildQueueRows(const std::vector<CUnit *> &units)
{
  for (CUnit *p : units) {
    const bool bBuilding = BuildingType(p->wType);
    const unsigned nMorph = MorphTarget(p);
    if (bBuilding && (p->dwFlags & 1) == 0 && !UnitTypeValid(nMorph)) {
      AddQueueRow(p, ROW_CONSTRUCT, p->wType, 0, BuildPercent(p, p->wType));
      continue;
    }
    if (UnitTypeValid(nMorph)) {
      AddQueueRow(p, ROW_MORPH, nMorph, 0, BuildPercent(p, nMorph));
      if (!QueueSource(p))
        continue;
    } else if ((p->dwFlags & 1) == 0) {
      continue;  // a trained unit is represented by its source's queue, not duplicated here
    }
    if (QueueSource(p)) {
      for (int i = UnitTypeValid(nMorph) ? 1 : 0; i < 5; ++i) {
        const unsigned t = p->GetBuildUnit(i);
        if (t == UNIT_TYPES_117 || !UnitTypeValid(t))
          continue;
        int nProgress = -1;
        if (i == 0) {
          CUnit *pMaking = p->GetConstructionTarget();
          if (pMaking != NULL && (pMaking->wType == t || pMaking->GetTypeEx() == t))
            nProgress = BuildPercent(pMaking, t);
        }
        AddQueueRow(p, ROW_TRAIN, t, i, nProgress);
      }
    }
    if (bBuilding && (p->dwFlags & 1) != 0) {
      if (p->bTechType != TECH_NONE && TechTypeValid(p->bTechType))
        AddQueueResearch(p, false, p->bTechType);
      if (p->bUpgradeType != UPGRADE_NONE && UpgradeTypeValid(p->bUpgradeType))
        AddQueueResearch(p, true, p->bUpgradeType);
    }
  }
}

void ClearOverride(void)
{
  if (!g_bOwnOverride)
    return;
  SelectOnlySet(NULL);
  g_bOwnOverride = false;
}

void ResetTransient(void)
{
  ClearOverride();
  g_rows[PANEL_RESEARCH].clear();
  g_rows[PANEL_QUEUE].clear();
  g_hits.clear();
  memset(g_anScroll, 0, sizeof g_anScroll);
  memset(g_layout, 0, sizeof g_layout);
  g_bPressed = false;
  g_bRowsValid = false;
  g_bLayoutValid = false;
}

void TickState(void)
{
  if (gwGameMode != 3) {
    if (g_bInGame)
      ResetTransient();
    g_bInGame = false;
    return;
  }
  if (!g_bInGame || oc_dwReplayFrame < g_dwLastFrame)
    ResetTransient();
  g_bInGame = true;
  g_dwLastFrame = oc_dwReplayFrame;
  if (g_bOwnOverride && (oc_dwReplayFrame >= g_dwOverrideClear || oc_dwReplayFrame < g_dwOverrideFrame))
    ClearOverride();
}

bool PanelShown(int nPanel)
{
  if (gwGameMode != 3 || LocalPlayer() < 0)
    return false;
  return nPanel == PANEL_RESEARCH ? g_bResearch && MutatorsOfGame().nNewbie != 0 : g_bQueue;
}

bool AnyShown(void)
{
  return PanelShown(PANEL_RESEARCH) || PanelShown(PANEL_QUEUE);
}

void RefreshRows(void)
{
  TickState();
  if (!AnyShown())
    return;
  if (g_bRowsValid && g_dwRowsFrame == oc_dwReplayFrame)
    return;
  g_rows[PANEL_RESEARCH].clear();
  g_rows[PANEL_QUEUE].clear();
  std::vector<CUnit *> units;
  const int nPlayer = LocalPlayer();
  for (CUnit *p = CUnit::GetUsedList(); p != NULL; p = p->pNext)
    if (p->bPlayer == nPlayer)
      units.push_back(p);
  if (PanelShown(PANEL_RESEARCH))
    BuildResearchRows(units, nPlayer);
  if (PanelShown(PANEL_QUEUE))
    BuildQueueRows(units);
  g_bRowsValid = true;
  g_dwRowsFrame = oc_dwReplayFrame;
  g_bLayoutValid = false;
}

void MakeLayout(void)
{
  RefreshRows();
  if (g_bLayoutValid)
    return;
  g_bLayoutValid = true;
  const bool bBoth = PanelShown(PANEL_RESEARCH) && PanelShown(PANEL_QUEUE);
  const int nViewW = SDL_max(640, g_portView.nWidth);
  const int nMapRows = SDL_max(240, g_portView.nMapRows);
  for (int nPanel = 0; nPanel < PANELS; ++nPanel) {
    PanelLayout &l = g_layout[nPanel];
    l.bShown = PanelShown(nPanel);
    l.nVisible = 0;
    if (!l.bShown)
      continue;
    const int nRows = static_cast<int>(g_rows[nPanel].size());
    l.nCapacity = SDL_min(bBoth ? 8 : 9, SDL_max(1, (nMapRows - 20 - kHeaderH - kFooterH) / kRowH));
    const int nMost = SDL_max(0, nRows - l.nCapacity);
    g_anScroll[nPanel] = SDL_clamp(g_anScroll[nPanel], 0, nMost);
    l.nFirst = g_anScroll[nPanel];
    l.nVisible = nRows > 0 ? SDL_min(l.nCapacity, nRows - l.nFirst) : 1;  // one empty-message row
    l.box.w = kWidth;
    l.box.h = kHeaderH + l.nVisible * kRowH + kFooterH;
    l.box.x = nPanel == PANEL_RESEARCH ? 8 : nViewW - kWidth - 8;
    l.box.y = SDL_max(4, SDL_min(12, nMapRows - l.box.h - 4));
    for (int i = 0; i < l.nVisible && nRows > 0; ++i) {
      l.rows[i].nRow = l.nFirst + i;
      l.rows[i].icon = {l.box.x + 6, l.box.y + kHeaderH + i * kRowH + 2, kIcon, kIcon};
    }
  }
}

FBox ToTarget(const Box &b, float fX, float fY, float fW, float fH)
{
  const float sx = fW / SDL_max(1, g_portView.nWidth);
  const float sy = fH / SDL_max(1, g_portView.nHeight);
  return {fX + b.x * sx, fY + b.y * sy, b.w * sx, b.h * sy};
}

bool Inside(const FBox &b, float x, float y)
{
  return x >= b.x && y >= b.y && x < b.x + b.w && y < b.y + b.h;
}

void AddHit(const FBox &box, HitKind kind, int nPanel, const RowKey *pKey = NULL)
{
  Hit h;
  h.box = box;
  h.kind = kind;
  h.nPanel = nPanel;
  if (pKey != NULL)
    h.key = *pKey;
  g_hits.push_back(h);
}

bool HitAt(float x, float y, Hit *pOut)
{
  for (size_t i = g_hits.size(); i > 0; --i)
    if (Inside(g_hits[i - 1].box, x, y)) {
      *pOut = g_hits[i - 1];
      return true;
    }
  return false;
}

float ScaleY(float fH)
{
  return fH / SDL_max(1, g_portView.nHeight);
}

void Text(const char *psz, float x, float y, float size, UiColor colour, UiFace face = UI_FACE_TEXT)
{
  UiText(x, y, face, size, 0.0f, colour, psz);
}

void DrawProgress(const FBox &box, int nPercent, UiColor colour)
{
  if (nPercent < 0)
    return;
  UiFill(box.x, box.y, box.w, box.h, UiRgb(0x17232e, 0.95f), box.h * 0.5f);
  if (nPercent > 0)
    UiFill(box.x, box.y, box.w * nPercent / 100.0f, box.h, colour, box.h * 0.5f);
}

bool DrawOnePanel(int nPanel, float fX, float fY, float fW, float fH)
{
  PanelLayout &l = g_layout[nPanel];
  if (!l.bShown)
    return false;
  std::vector<Row> &rows = g_rows[nPanel];
  l.target = ToTarget(l.box, fX, fY, fW, fH);
  const float k = ScaleY(fH);
  const float sx = fW / SDL_max(1, g_portView.nWidth);
  const UiColor panel = UiRgb(0x0b141d, 0.94f);
  const UiColor alternate = UiRgb(0x122230, 0.90f);
  const UiColor line = UiRgb(0x35536a, 0.95f);
  const UiColor ink = UiRgb(0xe8f0f5);
  const UiColor dim = UiRgb(0x9aafbd);
  const UiColor gold = UiRgb(0xf0c56b);
  auto X = [&](int x) { return fX + x * sx; };
  auto Y = [&](int y) { return fY + y * k; };
  auto W = [&](int w) { return w * sx; };
  auto H = [&](int h) { return h * k; };
  AddHit(l.target, HIT_PANEL, nPanel);

  // The HD icons live in the game's HD pass, under this overlay. Leave their wells unpainted:
  // panel backgrounds, text and bars are drawn only in the header, footer, gaps and text column.
  UiFill(X(l.box.x), Y(l.box.y), W(l.box.w), H(kHeaderH), panel, 4.0f * k);
  UiFill(X(l.box.x), Y(l.box.y + l.box.h - kFooterH), W(l.box.w), H(kFooterH), panel, 4.0f * k);
  UiFill(X(l.box.x + 44), Y(l.box.y + kHeaderH), W(l.box.w - 44), H(l.box.h - kHeaderH - kFooterH), panel);
  UiFill(X(l.box.x), Y(l.box.y + kHeaderH), W(4), H(l.box.h - kHeaderH - kFooterH), panel);
  UiRing(l.target.x, l.target.y, l.target.w, l.target.h, line, 4.0f * k, SDL_max(1.0f, k));

  const char *pszTitle = nPanel == PANEL_RESEARCH ? "RESEARCH" : "PRODUCTION";
  Text(pszTitle, X(l.box.x + 10), Y(l.box.y + 16), 12.0f * k, gold, UI_FACE_BOLD);
  char szKey[48];
  const int nHotkey = nPanel == PANEL_RESEARCH ? PORT_HOTKEY_RESEARCH : PORT_HOTKEY_PRODUCTION;
  PortHotkeyName(PortHotkey(nHotkey), szKey, sizeof szKey);
  char szSub[96];
  snprintf(szSub, sizeof szSub, "%s | %s", nPanel == PANEL_RESEARCH ? "All completed buildings" : "All your queues",
           PortHotkey(nHotkey) != 0 ? szKey : "unbound");
  Text(szSub, X(l.box.x + 10), Y(l.box.y + 31), 8.0f * k, dim);

  const FBox close = {X(l.box.x + l.box.w - 26), Y(l.box.y + 7), W(18), H(18)};
  UiRing(close.x, close.y, close.w, close.h, line, 3.0f * k, SDL_max(1.0f, k));
  Text("x", close.x + 5.0f * k, close.y + 13.0f * k, 10.0f * k, dim, UI_FACE_BOLD);
  AddHit(close, HIT_CLOSE, nPanel);

  if (rows.empty()) {
    const char *psz = nPanel == PANEL_RESEARCH ? "No research currently available" : "Nothing in production";
    Text(psz, X(l.box.x + 12), Y(l.box.y + kHeaderH + 22), 10.0f * k, dim);
  }
  const bool bHdConsole = PortGpuHdConsoleShown() != 0;
  for (int i = 0; i < l.nVisible && !rows.empty(); ++i) {
    const int nRow = l.rows[i].nRow;
    Row &r = rows[nRow];
    const int ry = l.box.y + kHeaderH + i * kRowH;
    const FBox rowBox = {X(l.box.x), Y(ry), W(l.box.w), H(kRowH)};
    if ((i & 1) != 0)
      UiFill(X(l.box.x + 44), Y(ry), W(l.box.w - 44), H(kRowH), alternate);
    // Paint only the two-pixel gaps in the icon column, never over the HD item itself.
    UiFill(X(l.box.x + 4), Y(ry), W(40), H(2), panel);
    UiFill(X(l.box.x + 4), Y(ry + 34), W(40), H(2), panel);
    const FBox icon = ToTarget(l.rows[i].icon, fX, fY, fW, fH);
    const bool bFallback = !bHdConsole || !r.bHdItem;
    if (bFallback) {
      const UiColor badge = r.kind == ROW_UPGRADE ? UiRgb(0x493b67) : r.kind == ROW_RESEARCH ? UiRgb(0x244f64)
                            : r.kind == ROW_CONSTRUCT ? UiRgb(0x3f5534) : UiRgb(0x344b60);
      UiFill(icon.x, icon.y, icon.w, icon.h, badge, 3.0f * k);
      const std::string mark = Badge(r.name);
      const float bw = UiTextWidth(UI_FACE_BOLD, 10.0f * k, 0.0f, mark.c_str());
      Text(mark.c_str(), icon.x + (icon.w - bw) * 0.5f, icon.y + 21.0f * k, 10.0f * k, ink, UI_FACE_BOLD);
    }
    UiRing(icon.x, icon.y, icon.w, icon.h, line, 3.0f * k, SDL_max(1.0f, k));

    char szMain[192];
    if (r.kind == ROW_UPGRADE && r.nTargetLevel > 0)
      snprintf(szMain, sizeof szMain, "%s  Lv %d/%d", r.name.c_str(), r.nTargetLevel, r.nMaxLevel);
    else
      snprintf(szMain, sizeof szMain, "%s", r.name.c_str());
    char szContext[224];
    const char *pszAction = "";
    if (r.nQueue > 0)
      pszAction = " | queued";
    else if (r.nProgress >= 0)
      pszAction = "";
    else if (nPanel == PANEL_RESEARCH && !r.bActive)
      pszAction = Observer() ? " | view only" : " | click to start";
    if (r.nProgress >= 0)
      snprintf(szContext, sizeof szContext, "%s | %d%%%s", r.context.c_str(), r.nProgress, pszAction);
    else
      snprintf(szContext, sizeof szContext, "%s%s%s", r.context.c_str(), pszAction,
               r.nSources > 1 ? " | more sources" : "");
    const UiColor main = r.bActive && r.nProgress >= 0 ? gold : r.nQueue > 0 ? dim : ink;
    UiPushClip(X(l.box.x + 46), Y(ry), X(l.box.x + l.box.w - 4), Y(ry + kRowH));
    Text(szMain, X(l.box.x + 50), Y(ry + 13), 10.0f * k, main, UI_FACE_BOLD);
    Text(szContext, X(l.box.x + 50), Y(ry + 26), 8.0f * k, dim);
    const UiColor bar = r.kind == ROW_RESEARCH ? UiRgb(0x55b7d9) : r.kind == ROW_UPGRADE ? UiRgb(0xb497df)
                        : r.kind == ROW_CONSTRUCT ? UiRgb(0x82c66e) : UiRgb(0x6b9fd0);
    DrawProgress({X(l.box.x + 50), Y(ry + 31), W(l.box.w - 58), H(3)}, r.nProgress, bar);
    UiPopClip();
    if (r.nQueue > 0) {
      char szSlot[16];
      snprintf(szSlot, sizeof szSlot, "#%d", r.nQueue + 1);
      Text(szSlot, X(l.box.x + l.box.w - 28), Y(ry + 13), 8.0f * k, dim, UI_FACE_MONO);
    }
    const RowKey key = KeyOf(r);
    AddHit(rowBox, HIT_ROW, nPanel, &key);
  }

  char szRange[64];
  const int nRows = static_cast<int>(rows.size());
  snprintf(szRange, sizeof szRange, nRows == 0 ? "0 rows" : "%d-%d of %d | wheel", l.nFirst + 1,
           SDL_min(nRows, l.nFirst + l.nVisible), nRows);
  Text(szRange, X(l.box.x + 10), Y(l.box.y + l.box.h - 7), 8.0f * k, dim);
  const FBox up = {X(l.box.x + l.box.w - 52), Y(l.box.y + l.box.h - 18), W(18), H(16)};
  const FBox down = {X(l.box.x + l.box.w - 28), Y(l.box.y + l.box.h - 18), W(18), H(16)};
  Text("^", up.x + 5.0f * k, up.y + 12.0f * k, 10.0f * k, g_anScroll[nPanel] > 0 ? ink : dim, UI_FACE_BOLD);
  Text("v", down.x + 5.0f * k, down.y + 12.0f * k, 10.0f * k,
       l.nFirst + l.nCapacity < nRows ? ink : dim, UI_FACE_BOLD);
  AddHit(up, HIT_UP, nPanel);
  AddHit(down, HIT_DOWN, nPanel);
  return true;
}

bool LayerShown(void)
{
  TickState();
  return AnyShown();
}

bool LayerDraw(int, int, float fX, float fY, float fW, float fH)
{
  TickState();
  if (!AnyShown() || fW <= 0.0f || fH <= 0.0f)
    return false;
  MakeLayout();
  g_hits.clear();
  bool bDrew = false;
  for (int nPanel = 0; nPanel < PANELS; ++nPanel)
    bDrew |= DrawOnePanel(nPanel, fX, fY, fW, fH);
  return bDrew;
}

bool LayerOutside(float x, float y)
{
  if (!AnyShown())
    return false;
  for (int nPanel = 0; nPanel < PANELS; ++nPanel)
    if (g_layout[nPanel].bShown && Inside(g_layout[nPanel].target, x, y))
      return true;
  return false;
}

void TogglePanel(int nPanel)
{
  bool &b = nPanel == PANEL_RESEARCH ? g_bResearch : g_bQueue;
  b = !b;
  g_anScroll[nPanel] = 0;
  g_bPressed = false;
  g_hits.clear();
  g_bRowsValid = false;  // a panel shown for the first time this frame needs its own scan
  g_bLayoutValid = false;
  memset(&g_layout[nPanel], 0, sizeof g_layout[nPanel]);
}

void CentreSource(DWORD dwId, bool bSelect)
{
  CUnit *p = Source(dwId);
  if (p == NULL || p->pSprite == NULL)
    return;
  if (bSelect && !Observer() && !SelectOnlyActive())
    send_net_cmd_Select(1, &p);
  p->CenterView();  // the ordinary portrait/selection camera action; never a game command
}

void ActivateResearch(const RowKey &key)
{
  MakeLayout();
  for (const Row &r : g_rows[PANEL_RESEARCH]) {
    if (!SameKey(KeyOf(r), key))
      continue;
    if (r.bActive || Observer()) {
      CentreSource(r.dwSource, !Observer());
      return;
    }
    if (SelectOnlyActive())  // another aimed command has not executed yet
      return;
    CUnit *p = Source(r.dwSource);
    if (p == NULL || !ResearchFree(p))
      return;
    const int nPlayer = LocalPlayer();
    if (r.kind == ROW_RESEARCH) {
      if (ResearchAllowed(r.id, nPlayer, p) != 1)
        return;
      (void)TechUseAllowed(r.id, nPlayer, p);
      SelectOnlySet(p);
      send_net_cmd_Research(static_cast<tech_id_t>(r.id));
    } else {
      if (UpgradeAllowed(r.id, nPlayer, p) != 1)
        return;
      SelectOnlySet(p);
      send_net_cmd_Upgrade(static_cast<upgrade_id_t>(r.id));
    }
    g_bOwnOverride = true;
    g_dwOverrideFrame = oc_dwReplayFrame;
    g_dwOverrideClear = oc_dwReplayFrame + 2;
    g_bRowsValid = false;
    return;
  }
}

void Activate(const Hit &hit)
{
  if (hit.kind == HIT_CLOSE) {
    TogglePanel(hit.nPanel);
    return;
  }
  if (hit.kind == HIT_UP || hit.kind == HIT_DOWN) {
    g_anScroll[hit.nPanel] += hit.kind == HIT_UP ? -1 : 1;
    g_bLayoutValid = false;
    MakeLayout();
    return;
  }
  if (hit.kind != HIT_ROW)
    return;
  if (hit.nPanel == PANEL_RESEARCH) {
    ActivateResearch(hit.key);
    return;
  }
  CentreSource(hit.key.dwSource, !Observer());
}

bool LayerMouse(unsigned uMsg, unsigned long long, long long lParam)
{
  TickState();
  const float x = static_cast<float>(static_cast<short>(lParam & 0xFFFF));
  const float y = static_cast<float>(static_cast<short>((lParam >> 16) & 0xFFFF));
  g_fMouseX = x;
  g_fMouseY = y;
  const bool bButton = uMsg >= 0x201 && uMsg <= 0x209;
  if (!bButton)
    return false;
  const bool bInside = LayerOutside(x, y);
  if (!bInside) {
    if (uMsg == 0x202)
      g_bPressed = false;
    return false;
  }
  if (uMsg == 0x201) {  // left down: remember exactly which row/button, never act on the press
    g_bPressed = HitAt(x, y, &g_pressed);
    return true;
  }
  if (uMsg == 0x202) {  // left up: act only on the same live rectangle
    Hit now;
    const bool bHit = HitAt(x, y, &now);
    if (g_bPressed && bHit && g_pressed.kind == now.kind && g_pressed.nPanel == now.nPanel &&
        (g_pressed.kind != HIT_ROW || SameKey(g_pressed.key, now.key)))
      Activate(now);
    g_bPressed = false;
    return true;
  }
  return true;  // right/middle clicks inside a panel are not map orders
}

bool LayerWheel(int nDelta)
{
  if (!LayerOutside(g_fMouseX, g_fMouseY))
    return false;
  for (int nPanel = 0; nPanel < PANELS; ++nPanel) {
    if (!g_layout[nPanel].bShown || !Inside(g_layout[nPanel].target, g_fMouseX, g_fMouseY))
      continue;
    g_anScroll[nPanel] -= nDelta > 0 ? 3 : nDelta < 0 ? -3 : 0;
    g_bLayoutValid = false;
    MakeLayout();
    return true;
  }
  return false;
}

const PortOverlayLayer kLayer = {"production", 350, PORT_OVERLAY_CONSOLE, LayerShown, NULL, LayerDraw, NULL,
                                 LayerMouse, NULL, LayerOutside, LayerWheel};
PORT_OVERLAY_LAYER(kLayer);

}  // namespace

bool PortProductionHotkey(int nHotkey)
{
  TickState();
  if (gwGameMode != 3)
    return false;
  if (nHotkey == PORT_HOTKEY_RESEARCH) {
    if (MutatorsOfGame().nNewbie == 0)
      return false;
    TogglePanel(PANEL_RESEARCH);
    return true;
  }
  if (nHotkey == PORT_HOTKEY_PRODUCTION) {
    TogglePanel(PANEL_QUEUE);  // deliberately independent of Newbie mode
    return true;
  }
  return false;
}

int PortProductionHdItems(PortHdUiItem *pItems, int nMax)
{
  if (pItems == NULL || nMax <= 0)
    return 0;
  TickState();
  if (!AnyShown())
    return 0;
  MakeLayout();
  for (int nPanel = 0; nPanel < PANELS; ++nPanel)
    for (Row &r : g_rows[nPanel])
      r.bHdItem = false;
  int n = 0;
  for (int nPanel = 0; nPanel < PANELS; ++nPanel) {
    PanelLayout &l = g_layout[nPanel];
    if (!l.bShown || g_rows[nPanel].empty())
      continue;
    for (int i = 0; i < l.nVisible && n < nMax; ++i) {
      const int nRow = l.rows[i].nRow;
      if (nRow < 0 || nRow >= static_cast<int>(g_rows[nPanel].size()))
        continue;
      Row &r = g_rows[nPanel][nRow];
      if (r.nIcon < 0)
        continue;
      const bool bOwn = ModArtOwnFrame(MOD_ART_ICON, static_cast<unsigned>(r.nIcon));
      if (bOwn ? !PortModUiHas(MOD_ART_ICON, static_cast<unsigned>(r.nIcon)) : PortHdUiSet(PORT_HD_UI_ICONS) == NULL)
        continue;  // the overlay's badge remains until this frame/art is ready
      PortHdUiItem &item = pItems[n++];
      memset(&item, 0, sizeof item);
      item.nSet = bOwn ? PORT_HD_UI_MOD : PORT_HD_UI_ICONS;
      item.nFrame = bOwn ? (MOD_ART_ICON << 16) | r.nIcon : r.nIcon;
      item.nCell = bOwn ? 0 : -1;
      item.nX = l.rows[i].icon.x;
      item.nY = l.rows[i].icon.y;
      item.nW = l.rows[i].icon.w;
      item.nH = l.rows[i].icon.h;
      item.nTints = 16;
      memcpy(item.abTint, sgBmpTIcon_g + 3 * 16, 16);
      r.bHdItem = true;
    }
  }
  return n;
}
