// port_mutators.cpp -- a game's mutators (h/mutators.h; port_mutators.h).
//
// The skirmish set-up's mutators as they play: one set for every player, chosen with the game
// and held in its replay and save. The game asks for each value where 1.17 has its own
// (h/mutators.h), which this file answers from the game's set; a game without one never gets
// here (g_hxMutatorsOn).
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "port_mutators.h"
#include "port_richbases.h"     // RichBasesMapLoaded
#include "../../h/CUnit.h"
#include "../../h/CSprite.h"
#include "../../h/CThingy.h"    // the doodads' pool, for Explored + resources
#include "../front/port_text.h"
#include "../platform/port_json.h"  // PortJson
#include "../platform/port_log.h"   // PortLogger
#include "../../h/mutators.h"
#include "../../h/hxrules.h"        // HX_OPT_MUTATORS, g_hxRuleOptions
#include "../../h/playerlimits.h"   // PlayerSlots, PLAYER_LOBBY_117
#include "../../h/playertables.h"
#include "../../h/playervision.h"   // VisionHxHidden, VisionHxUnexplored
#include "../../h/net_cmds.h"
#include "../../h/saveload.h"       // the tech and upgrade tables' accessors
#include "../../h/typelimits.h"     // UnitTypeValid, UnitTypeMod, TechTypeValid, UpgradeTypeValid
#include "../../h/CUnit.h"
#include "../../h/poollimits.h"     // POOL_UNITS_117, POOL_UNITS_MAX
#include "../../gen/globals117.h"   // gbMultiPlayerMode, gPlayback, gpLoadGameFile, the time tables, gpSquareMap

bool g_hxMutatorsOn = false;

namespace {

const PortLogger Log("hyperion_mutators.log");

// a cap the mutator lifts, in halves: past any supply a game reaches, and a team captain's
// cap (count_init, a slot's cap times the team's slots) still fits an int
const int kUnlimitedHalves = 0x1000000;

const int kShares[] = {50, 100, 150, 200, 300, 400, 800, 0};  // 0 never runs dry
const int kStarts[][2] = {{50, 0}, {500, 200}, {1000, 500}, {5000, 5000}, {10000, 10000}};
// the set-up's lists since 0.6.16: the user, 2026-10-25, workers "in steps of 2 up to 12, and as
// low as 2", the cap "in steps of 100 - 600 being max", and build time Classic and faster only.
// The values the lists left (Unlimited, 1 worker, x1.5) still play, from --mutators or an older
// replay, save or room; the set-up shows them as they are (MutatorValueName)
const int kCaps[] = {100, 200, 300, 400, 500, 600};
const int kBoosts[] = {0, 25, 50, 75, 100};
const int kWorkers[] = {2, 4, 6, 8, 10, 12};
const int kWorkersMost = 12;
const char *const kFogs[] = {PT_("Normal"), PT_("Explored"), PT_("None"), PT_("Explored + resources")};  // MUTATOR_FOG_*
const char *const kNoArmyKeys[] = {PT_("Off"), PT_("On")};                       // the army hotkey's mutator
const char *const kEnergyRegens[] = {PT_("Normal"), PT_("x3")};                  // the energy regen's mutator
const char *const kNewbies[] = {PT_("Off"), PT_("On")};                          // the newbie mode's mutator
const char *const kAutoWorkers[] = {PT_("Off"), PT_("On")};                      // auto-train workers
const char *const kAutoMines[] = {PT_("Off"), PT_("On")};                        // auto-mine efficiently
const int kBuilds[] = {25, 50, 75, 100};                        // a percent of 1.17's times
const char *const kTechs[] = {PT_("Normal"), PT_("Researched"), PT_("No upgrades"),
                              PT_("No research")};  // MUTATOR_TECH_*
const char *const kRiches[] = {PT_("Off"), PT_("Past the naturals"), PT_("Outside a radius"), PT_("Farthest only"),
                               PT_("Every base")};  // MUTATOR_RICH_*
const int kChances[] = {25, 50, 75, 100};
// the radius round every start: tiles, or a share of the map's size (the user, 2026-10-26: "options
// for tiles and share of the map as a dual option")
const int kRadii[] = {32, 48, 64, 80, -25, -33, -50};
// the unit limit, Automatic and doublings of 1.17's table up to the most: a computer can
// field upwards of 600 units with the mutator, so the table goes much higher
const int kUnitLimits[] = {0, 3400, 6800, 13600, 27200, 54400, 108800, POOL_UNITS_MAX};

// the no-cap mutator's choices and the one add-on's (the user, 2026-11-02): Classic's,
// or the new rule
const char *const kNocaps[] = {PT_("Classic"), PT_("No cap")};    // MUT_NOCAP
const char *const kAddons[] = {PT_("Classic"), PT_("One add-on")};  // MUT_ADDON

// the types the no-cap mutator cheapens and halves (the user, 2026-11-02): 1.17's Supply
// Depot (109) and Pylon (156), 40 minerals each, and half the life, a pylon's half its
// shields too (gxUnitLife in 256ths, the shields in guwUnitEnergy)
const int kNoCapTypes[] = {109, 156};
const int kNoCapMinerals = 40;

const char *const kNames[MUT_COUNT] = {PT_("Mineral fields"), PT_("Vespene geysers"), PT_("Start resources"),
                                       PT_("Supply cap"),     PT_("Start workers"),   PT_("Supply boost"),
                                       PT_("Fog of war"),     PT_("Build time"),      PT_("Tech and upgrades"),
                                       PT_("Rich resources"), PT_("No cap"),         PT_("One add-on"),
                                       PT_("No army key"), PT_("Energy regeneration"), PT_("Newbie mode"),
                                       PT_("Auto train workers"), PT_("Auto mine efficiently")};
const char *const kLines[MUT_COUNT] = {
    PT_("What each mineral field holds, or that it never runs dry. Classic: 1,500, the small ones 750."),
    PT_("What each geyser holds before it runs dry, or that it never does. Classic: 5,000."),
    PT_("The minerals and gas each player begins with. Classic: 50 and 0."),
    PT_("The most supply each player can hold. Classic: 200."),
    PT_("The workers each player begins with, beside the town hall. Classic: 4."),
    PT_("More supply from each depot, pylon, overlord and town hall. Classic: none, a depot\xe2\x80\x99s 8."),
    PT_("Explored: every player knows the map\xe2\x80\x99s land from the start, under the fog. None: every "
        "player sees the whole map all game. Classic: the fog of war."),
    PT_("How long buildings, units, research and upgrades take to make: \xc3\x97" "0.5 takes half as long. "
        "Classic: \xc3\x97" "1."),
    PT_("Researched: every upgrade at its top level and every ability researched from the start. No "
        "upgrades: none can be researched. No research: neither upgrades nor the abilities that need "
        "research. Classic: all to research."),
    PT_("Bases away from every start may turn rich: gold mineral fields and violet geysers that give 11 "
        "minerals and 16 gas a trip, and run dry sooner. Never a start's main or natural; bases that mirror "
        "each other turn rich together. Every base: all of them. Classic: none."),
    PT_("Supply is not needed: every unit costs no supply and the cap is off, and a depot and a pylon cost "
        "40 minerals with half their life, as they are built for their tech, not their supply. Classic: "
        "supply as 1.17's."),
    PT_("One add-on of a kind serves every building: one Machine Shop lets any Factory build Siege Tanks, "
        "and the same for the Control Tower's, the Covert Ops' and the Physics Lab's. Classic: each "
        "building needs its own."),
    PT_("No select-army key and no army button, for a game played without them. Classic: the key and the button."),
    PT_("Every unit's energy comes back three times as fast. Classic: the usual rate."),
    PT_("Newbie helpers for a player learning the game: workers, mining and research by themselves. The "
        "toggles are in Settings' Newbie mode tab."),
    PT_("A completed town hall with nothing training and supply to spare trains its worker by itself."),
    PT_("Each base's patches take up to three workers, each geyser three; once a base is full, the workers "
        "that are free go to the next base with room, and to the built geysers."),
};

// the presets' names, which --mutators also reads (MutatorsChooseList) from the English
const char *const kPresetNames[] = {PT_("Standard"), PT_("Plenty"), PT_("Gold Rush"), PT_("Infinite"),
                                    PT_("Quick Start")};

MutatorSet g_game;      // the game being played
int g_nRevision;        // and the rules' revision it plays by (MUTATOR_REVISION)
MutatorSet g_recorded;  // the last replay's or save's record
int g_nRecordedRevision;
bool g_bRecorded;       // and whether it had one
MutatorSet g_net;       // a room's match
bool g_bNet;

// The room's choice is for a game over the network; a launch that failed before its game
// leaves it unread, and a game in single player then takes the session's.
bool NetChosen() {
  return g_bNet && gbMultiPlayerMode != 0;
}

int Clamp(int n, int nLeast, int nMost) {
  return n < nLeast ? nLeast : n > nMost ? nMost : n;
}

// A share as the set-up writes it: x2, x1.5, x0.5.
std::string Share(int nPercent) {
  if (nPercent == 0)
    return PortText("Infinite");
  char sz[16];
  if (nPercent % 100 == 0)
    snprintf(sz, sizeof sz, "\xc3\x97%d", nPercent / 100);
  else if (nPercent % 10 == 0)
    snprintf(sz, sizeof sz, "\xc3\x97%d.%d", nPercent / 100, nPercent / 10 % 10);
  else
    snprintf(sz, sizeof sz, "\xc3\x97%d.%02d", nPercent / 100, nPercent % 100);
  return sz;
}

// A count with thousands' commas: 10,000.
std::string Count(int n) {
  std::string s = std::to_string(n);
  for (int i = static_cast<int>(s.size()) - 3; i > 0; i -= 3)
    s.insert(static_cast<size_t>(i), ",");
  return s;
}

// The index of the choice nearest nValue among n values, where 0 is past every other.
int Nearest(const int *pValues, int n, int nValue) {
  int nBest = 0;
  long long llBest = -1;
  for (int i = 0; i < n; i++) {
    const long long a = pValues[i] == 0 ? 1LL << 40 : pValues[i];
    const long long b = nValue == 0 ? 1LL << 40 : nValue;
    const long long d = a > b ? a - b : b - a;
    if (llBest < 0 || d < llBest) {
      llBest = d;
      nBest = i;
    }
  }
  return nBest;
}

// A preset's name as --mutators may write it: its letters, lower case ("gold-rush", "GoldRush").
std::string Bare(const char *psz) {
  std::string s;
  for (; *psz; ++psz)
    if ((*psz >= 'a' && *psz <= 'z') || (*psz >= 'A' && *psz <= 'Z'))
      s += static_cast<char>(*psz | 0x20);
  return s;
}

// Rich bases' chance and rules count only when a roll is made.
bool RichRolled(const MutatorSet &m) {
  return m.nRich != MUTATOR_RICH_OFF && m.nRich != MUTATOR_RICH_EVERY;
}

bool Same(const MutatorSet &a, const MutatorSet &b) {
  return a.nMinerals == b.nMinerals && a.nGas == b.nGas && a.nStartMinerals == b.nStartMinerals &&
         a.nStartGas == b.nStartGas && a.nSupplyCap == b.nSupplyCap && a.nWorkers == b.nWorkers &&
         a.nSupplyBoost == b.nSupplyBoost && a.nFog == b.nFog && a.nBuildTime == b.nBuildTime &&
         a.nTech == b.nTech && a.nRich == b.nRich &&
         (!RichRolled(a) || (a.nRichChance == b.nRichChance && a.nRichRules == b.nRichRules)) &&
         (a.nRich == MUTATOR_RICH_OFF || (a.nRichRules & MUTATOR_RICH_QUIET) == (b.nRichRules & MUTATOR_RICH_QUIET)) &&
         (a.nRich != MUTATOR_RICH_RADIUS || a.nRichRadius == b.nRichRadius) && a.nUnits == b.nUnits &&
         a.nNoCap == b.nNoCap && a.nAddon == b.nAddon && a.nNoArmyKey == b.nNoArmyKey &&
         a.nEnergyRegen == b.nEnergyRegen && a.nNewbie == b.nNewbie && a.nAutoWorkers == b.nAutoWorkers &&
         a.nAutoMine == b.nAutoMine && a.dwAutoUpgLo == b.dwAutoUpgLo && a.dwAutoUpgHi == b.dwAutoUpgHi;
}

// A radius in words: "48 tiles", "a quarter of the map"; in English for a log or a key.
std::string RadiusName(int nRadius, bool bShown = true) {
  const auto t = [bShown](const char *psz) -> std::string { return bShown ? PortText(psz) : psz; };
  switch (nRadius) {
  case -25: return t(PT_("a quarter of the map"));
  case -33: return t(PT_("a third of the map"));
  case -50: return t(PT_("half the map"));
  }
  return nRadius < 0 ? PortTextFill(t(PT_("{0}% of the map")).c_str(), {std::to_string(-nRadius)})
                     : PortTextFill(t(PT_("{0} tiles")).c_str(), {std::to_string(nRadius)});
}

// The short name the card's chips show.
std::string RadiusChip(int nRadius) {
  switch (nRadius) {
  case -25: return PortText("Quarter map");
  case -33: return PortText("Third map");
  case -50: return PortText("Half map");
  }
  return nRadius < 0 ? PortTextF("{0}% of map", {std::to_string(-nRadius)})
                     : PortTextF("{0} tiles", {std::to_string(nRadius)});
}

// "explored", "none" or a number for the fog; "all", "noupgrades", "noresearch" or a number
// for the tech; anything else the classic one.
int ParseFog(const char *psz) {
  if (_strnicmp(psz, "resources", 9) == 0 || _strnicmp(psz, "exploredresources", 16) == 0)
    return MUTATOR_FOG_RESOURCES;
  if (_strnicmp(psz, "exp", 3) == 0)
    return MUTATOR_FOG_EXPLORED;
  if (_strnicmp(psz, "none", 4) == 0 || _strnicmp(psz, "off", 3) == 0)
    return MUTATOR_FOG_NONE;
  return atoi(psz);
}

int ParseTech(const char *psz) {
  if (_strnicmp(psz, "all", 3) == 0 || _strnicmp(psz, "max", 3) == 0)
    return MUTATOR_TECH_ALL;
  if (_strnicmp(psz, "noup", 4) == 0)
    return MUTATOR_TECH_NO_UPGRADES;
  if (_strnicmp(psz, "nores", 5) == 0 || _strnicmp(psz, "none", 4) == 0)
    return MUTATOR_TECH_NO_RESEARCH;
  return atoi(psz);
}

// "off", "naturals", "radius" (or "middle", its place before 0.6.21), "far" or "every", or a
// number.
int ParseRich(const char *psz) {
  if (_strnicmp(psz, "nat", 3) == 0 || _strnicmp(psz, "past", 4) == 0)
    return MUTATOR_RICH_NATURALS;
  if (_strnicmp(psz, "rad", 3) == 0 || _strnicmp(psz, "out", 3) == 0 || _strnicmp(psz, "mid", 3) == 0)
    return MUTATOR_RICH_RADIUS;
  if (_strnicmp(psz, "far", 3) == 0)
    return MUTATOR_RICH_FAR;
  if (_strnicmp(psz, "every", 5) == 0 || _strnicmp(psz, "all", 3) == 0)
    return MUTATOR_RICH_EVERY;
  return atoi(psz);
}

// A radius: tiles ("64", "64tiles"), "quarter", "third" or "half" of the map, or a share
// ("40%").
int ParseRadius(const char *psz) {
  if (_strnicmp(psz, "quarter", 7) == 0)
    return -25;
  if (_strnicmp(psz, "third", 5) == 0)
    return -33;
  if (_strnicmp(psz, "half", 4) == 0)
    return -50;
  const int n = atoi(psz);
  return strchr(psz, '%') != NULL ? -n : n;
}

// "off", "no" or "0" turn a rule off.
bool ParseOn(const char *psz) {
  return !(_strnicmp(psz, "off", 3) == 0 || _strnicmp(psz, "no", 2) == 0 || strcmp(psz, "0") == 0);
}

// A time at the build time mutator: at least 1 where 1.17's is, and at most a WORD's.
WORD Timed(WORD w) {
  if (w == 0)
    return 0;
  const long long ll = (static_cast<long long>(w) * g_game.nBuildTime + 50) / 100;
  return static_cast<WORD>(ll < 1 ? 1 : ll > 0xFFFF ? 0xFFFF : ll);
}

// "endless", "inf" or "infinite" for 0; "x2", "2", "200%" or "1.5" as a percent.
int ParseShare(const char *psz) {
  if (_strnicmp(psz, "inf", 3) == 0 || _strnicmp(psz, "endless", 7) == 0)
    return 0;
  if (*psz == 'x' || *psz == 'X')
    ++psz;
  char *pEnd = NULL;
  const double d = strtod(psz, &pEnd);
  if (pEnd == psz)
    return 100;
  return *pEnd == '%' || d > 20 ? static_cast<int>(d + 0.5) : static_cast<int>(d * 100 + 0.5);
}

#pragma pack(push, 1)
struct Record {
  DWORD dwMinerals, dwGas, dwStartMinerals, dwStartGas, dwSupplyCap, dwWorkers, dwSupplyBoost;
  DWORD dwFog, dwBuildTime, dwTech;
  DWORD dwRevision;
  DWORD dwRich, dwRichChance, dwRichRules;
  DWORD dwRichRadius;
  DWORD dwUnits;
  DWORD dwNoCap, dwAddon;
  DWORD dwNoArmyKey, dwEnergyRegen;
  DWORD dwNewbie, dwAutoWorkers, dwAutoMine, dwAutoUpgLo, dwAutoUpgHi;
};
#pragma pack(pop)
static_assert(sizeof(Record) == MUTATOR_RECORD_BYTES_NEWBIE, "the record's size is fixed");

}  // namespace

bool MutatorsStandard(const MutatorSet &m) {
  return Same(m, MutatorSet());
}

MutatorSet MutatorsClamp(const MutatorSet &m) {
  MutatorSet c;
  c.nMinerals = m.nMinerals == 0 ? 0 : Clamp(m.nMinerals, 10, 800);
  c.nGas = m.nGas == 0 ? 0 : Clamp(m.nGas, 10, 800);
  c.nStartMinerals = Clamp(m.nStartMinerals, 0, 1000000);
  c.nStartGas = Clamp(m.nStartGas, 0, 1000000);
  c.nSupplyCap = m.nSupplyCap == 0 ? 0 : Clamp(m.nSupplyCap, 1, 100000);
  c.nWorkers = Clamp(m.nWorkers, 1, kWorkersMost);
  c.nSupplyBoost = (Clamp(m.nSupplyBoost, 0, 100) + 12) / 25 * 25;
  c.nFog = Clamp(m.nFog, MUTATOR_FOG_CLASSIC, MUTATOR_FOG_RESOURCES);
  c.nBuildTime = Clamp(m.nBuildTime, 10, 400);
  c.nTech = Clamp(m.nTech, MUTATOR_TECH_CLASSIC, MUTATOR_TECH_NO_RESEARCH);
  c.nRich = Clamp(m.nRich, MUTATOR_RICH_OFF, MUTATOR_RICH_EVERY);
  c.nRichChance = (Clamp(m.nRichChance, 25, 100) + 12) / 25 * 25;
  c.nRichRules =
      m.nRichRules & (MUTATOR_RICH_FAIR | MUTATOR_RICH_AT_LEAST_ONE | MUTATOR_RICH_SAME_MAP | MUTATOR_RICH_QUIET);
  c.nRichRadius = m.nRichRadius < 0 ? Clamp(m.nRichRadius, -75, -10) : m.nRichRadius == 0 ? c.nRichRadius
                                                                                          : Clamp(m.nRichRadius, 16, 256);
  c.nUnits = m.nUnits <= 0 ? 0 : Clamp(m.nUnits, POOL_UNITS_117, POOL_UNITS_MAX);
  c.nNoCap = m.nNoCap != 0;
  c.nAddon = m.nAddon != 0;
  c.nNoArmyKey = m.nNoArmyKey != 0;
  c.nEnergyRegen = m.nEnergyRegen != 0;
  c.nNewbie = m.nNewbie != 0;
  c.nAutoWorkers = m.nAutoWorkers != 0;
  c.nAutoMine = m.nAutoMine != 0;
  c.dwAutoUpgLo = m.dwAutoUpgLo;
  c.dwAutoUpgHi = m.dwAutoUpgHi;
  return c;
}

int MutatorUnitLimits(void) {
  return static_cast<int>(sizeof kUnitLimits / sizeof kUnitLimits[0]);
}

int MutatorUnitLimit(int i) {
  return i > 0 && i < MutatorUnitLimits() ? kUnitLimits[i] : 0;
}

std::string MutatorUnitLimitName(int i) {
  return i > 0 && i < MutatorUnitLimits() ? Count(kUnitLimits[i]) : std::string(PortText("Automatic"));
}

int MutatorUnitLimitOf(int nUnits) {
  if (nUnits <= 0)
    return 0;
  return 1 + Nearest(kUnitLimits + 1, MutatorUnitLimits() - 1, nUnits);
}

int MutatorsRevisionOf(void) {
  return g_nRevision;
}

int MutatorRichRadiusTiles(const MutatorSet &m, int nMapW, int nMapH) {
  return m.nRichRadius >= 0 ? m.nRichRadius : (nMapW + nMapH) * -m.nRichRadius / 200;
}

std::string MutatorRichRadiusName(const MutatorSet &m) {
  return RadiusName(m.nRichRadius);
}

int MutatorChoices(int nMut) {
  switch (nMut) {
  case MUT_MINERALS:
  case MUT_GAS: return static_cast<int>(sizeof kShares / sizeof kShares[0]);
  case MUT_START: return static_cast<int>(sizeof kStarts / sizeof kStarts[0]);
  case MUT_SUPPLY: return static_cast<int>(sizeof kCaps / sizeof kCaps[0]);
  case MUT_WORKERS: return static_cast<int>(sizeof kWorkers / sizeof kWorkers[0]);
  case MUT_BOOST: return static_cast<int>(sizeof kBoosts / sizeof kBoosts[0]);
  case MUT_FOG: return static_cast<int>(sizeof kFogs / sizeof kFogs[0]);
  case MUT_BUILD: return static_cast<int>(sizeof kBuilds / sizeof kBuilds[0]);
  case MUT_TECH: return static_cast<int>(sizeof kTechs / sizeof kTechs[0]);
  case MUT_RICH: return static_cast<int>(sizeof kRiches / sizeof kRiches[0]);
  case MUT_NOCAP: return static_cast<int>(sizeof kNocaps / sizeof kNocaps[0]);
  case MUT_ADDON: return static_cast<int>(sizeof kAddons / sizeof kAddons[0]);
  case MUT_NOARMYKEY: return static_cast<int>(sizeof kNoArmyKeys / sizeof kNoArmyKeys[0]);
  case MUT_ENERGY: return static_cast<int>(sizeof kEnergyRegens / sizeof kEnergyRegens[0]);
  case MUT_NEWBIE: return static_cast<int>(sizeof kNewbies / sizeof kNewbies[0]);
  case MUT_AUTOWORKERS: return static_cast<int>(sizeof kAutoWorkers / sizeof kAutoWorkers[0]);
  case MUT_AUTOMINE: return static_cast<int>(sizeof kAutoMines / sizeof kAutoMines[0]);
  case MUT_RICH_CHANCE: return static_cast<int>(sizeof kChances / sizeof kChances[0]);
  case MUT_RICH_RADIUS: return static_cast<int>(sizeof kRadii / sizeof kRadii[0]);
  }
  return 0;
}

std::string MutatorChoiceName(int nMut, int i) {
  if (i < 0 || i >= MutatorChoices(nMut))
    return std::string();
  switch (nMut) {
  case MUT_MINERALS:
  case MUT_GAS: return Share(kShares[i]);
  case MUT_START: return Count(kStarts[i][0]) + " / " + Count(kStarts[i][1]);
  case MUT_SUPPLY: return Count(kCaps[i]);
  case MUT_WORKERS: return std::to_string(kWorkers[i]);
  case MUT_BOOST: return kBoosts[i] == 0 ? std::string(PortText("None")) : "+" + std::to_string(kBoosts[i]) + "%";
  case MUT_FOG: return PortText(kFogs[i]);
  case MUT_BUILD: return Share(kBuilds[i]);
  case MUT_TECH: return PortText(kTechs[i]);
  case MUT_RICH: return PortText(kRiches[i]);
  case MUT_NOCAP: return PortText(kNocaps[i]);
  case MUT_ADDON: return PortText(kAddons[i]);
  case MUT_NOARMYKEY: return PortText(kNoArmyKeys[i]);
  case MUT_ENERGY: return PortText(kEnergyRegens[i]);
  case MUT_NEWBIE: return PortText(kNewbies[i]);
  case MUT_AUTOWORKERS: return PortText(kAutoWorkers[i]);
  case MUT_AUTOMINE: return PortText(kAutoMines[i]);
  case MUT_RICH_CHANCE: return PortTextF("{0}% chance", {std::to_string(kChances[i])});
  case MUT_RICH_RADIUS: return RadiusChip(kRadii[i]);
  }
  return std::string();
}

int MutatorChoiceOf(const MutatorSet &m, int nMut) {
  switch (nMut) {
  case MUT_MINERALS: return Nearest(kShares, MutatorChoices(nMut), m.nMinerals);
  case MUT_GAS: return Nearest(kShares, MutatorChoices(nMut), m.nGas);
  case MUT_START: {
    int aMinerals[sizeof kStarts / sizeof kStarts[0]];
    for (int i = 0; i < MutatorChoices(nMut); i++) {
      if (kStarts[i][0] == m.nStartMinerals && kStarts[i][1] == m.nStartGas)
        return i;
      aMinerals[i] = kStarts[i][0];
    }
    return Nearest(aMinerals, MutatorChoices(nMut), m.nStartMinerals);
  }
  case MUT_SUPPLY: return Nearest(kCaps, MutatorChoices(nMut), m.nSupplyCap);
  case MUT_WORKERS: return Nearest(kWorkers, MutatorChoices(nMut), m.nWorkers);
  case MUT_BOOST: return Nearest(kBoosts, MutatorChoices(nMut), m.nSupplyBoost);
  case MUT_FOG: return Clamp(m.nFog, 0, MutatorChoices(nMut) - 1);
  case MUT_NOARMYKEY: return m.nNoArmyKey != 0 ? 1 : 0;
  case MUT_ENERGY: return m.nEnergyRegen != 0 ? 1 : 0;
  case MUT_NEWBIE: return m.nNewbie != 0 ? 1 : 0;
  case MUT_AUTOWORKERS: return m.nAutoWorkers != 0 ? 1 : 0;
  case MUT_AUTOMINE: return m.nAutoMine != 0 ? 1 : 0;
  case MUT_BUILD: return Nearest(kBuilds, MutatorChoices(nMut), m.nBuildTime);
  case MUT_TECH: return Clamp(m.nTech, 0, MutatorChoices(nMut) - 1);
  case MUT_RICH: return Clamp(m.nRich, 0, MutatorChoices(nMut) - 1);
  case MUT_NOCAP: return Clamp(m.nNoCap, 0, MutatorChoices(nMut) - 1);
  case MUT_ADDON: return Clamp(m.nAddon, 0, MutatorChoices(nMut) - 1);
  case MUT_RICH_CHANCE: return Nearest(kChances, MutatorChoices(nMut), m.nRichChance);
  case MUT_RICH_RADIUS:
    for (int i = 0; i < MutatorChoices(nMut); i++)
      if (kRadii[i] == m.nRichRadius)
        return i;
    return m.nRichRadius < 0 ? 4 + Nearest(kRadii + 4, 3, m.nRichRadius) : Nearest(kRadii, 4, m.nRichRadius);
  }
  return 0;
}

void MutatorChoose(MutatorSet *pm, int nMut, int i) {
  if (i < 0 || i >= MutatorChoices(nMut))
    return;
  switch (nMut) {
  case MUT_MINERALS: pm->nMinerals = kShares[i]; break;
  case MUT_GAS: pm->nGas = kShares[i]; break;
  case MUT_START:
    pm->nStartMinerals = kStarts[i][0];
    pm->nStartGas = kStarts[i][1];
    break;
  case MUT_SUPPLY: pm->nSupplyCap = kCaps[i]; break;
  case MUT_WORKERS: pm->nWorkers = kWorkers[i]; break;
  case MUT_BOOST: pm->nSupplyBoost = kBoosts[i]; break;
  case MUT_FOG: pm->nFog = i; break;
  case MUT_NOARMYKEY: pm->nNoArmyKey = i != 0 ? 1 : 0; break;
  case MUT_ENERGY: pm->nEnergyRegen = i != 0 ? 1 : 0; break;
  case MUT_NEWBIE: pm->nNewbie = i != 0 ? 1 : 0; break;
  case MUT_AUTOWORKERS: pm->nAutoWorkers = i != 0 ? 1 : 0; break;
  case MUT_AUTOMINE: pm->nAutoMine = i != 0 ? 1 : 0; break;
  case MUT_BUILD: pm->nBuildTime = kBuilds[i]; break;
  case MUT_TECH: pm->nTech = i; break;
  case MUT_RICH: pm->nRich = i; break;
  case MUT_NOCAP: pm->nNoCap = i; break;
  case MUT_ADDON: pm->nAddon = i; break;
  case MUT_RICH_CHANCE: pm->nRichChance = kChances[i]; break;
  case MUT_RICH_RADIUS: pm->nRichRadius = kRadii[i]; break;
  }
}

int MutatorChoiceExact(const MutatorSet &m, int nMut) {
  const int i = MutatorChoiceOf(m, nMut);
  MutatorSet t = m;
  MutatorChoose(&t, nMut, i);
  return Same(t, m) || (nMut == MUT_RICH_CHANCE && t.nRichChance == m.nRichChance) ||
                 (nMut == MUT_RICH_RADIUS && t.nRichRadius == m.nRichRadius)
             ? i
             : -1;
}

std::string MutatorValueName(const MutatorSet &m, int nMut) {
  if (MutatorChoiceExact(m, nMut) >= 0)
    return MutatorChoiceName(nMut, MutatorChoiceOf(m, nMut));
  switch (nMut) {
  case MUT_MINERALS: return Share(m.nMinerals);
  case MUT_GAS: return Share(m.nGas);
  case MUT_START: return Count(m.nStartMinerals) + " / " + Count(m.nStartGas);
  case MUT_SUPPLY: return m.nSupplyCap == 0 ? std::string(PortText("Unlimited")) : Count(m.nSupplyCap);
  case MUT_WORKERS: return std::to_string(m.nWorkers);
  case MUT_BOOST:
    return m.nSupplyBoost == 0 ? std::string(PortText("None")) : "+" + std::to_string(m.nSupplyBoost) + "%";
  case MUT_BUILD: return Share(m.nBuildTime);
  case MUT_RICH_CHANCE: return PortTextF("{0}% chance", {std::to_string(m.nRichChance)});
  case MUT_RICH_RADIUS: return RadiusChip(m.nRichRadius);
  }
  return MutatorChoiceName(nMut, MutatorChoiceOf(m, nMut));
}

bool MutatorOn(const MutatorSet &m, int nMut) {
  const MutatorSet c;
  switch (nMut) {
  case MUT_MINERALS: return m.nMinerals != c.nMinerals;
  case MUT_GAS: return m.nGas != c.nGas;
  case MUT_START: return m.nStartMinerals != c.nStartMinerals || m.nStartGas != c.nStartGas;
  case MUT_SUPPLY: return m.nSupplyCap != c.nSupplyCap;
  case MUT_WORKERS: return m.nWorkers != c.nWorkers;
  case MUT_BOOST: return m.nSupplyBoost != c.nSupplyBoost;
  case MUT_FOG: return m.nFog != c.nFog;
  case MUT_BUILD: return m.nBuildTime != c.nBuildTime;
  case MUT_TECH: return m.nTech != c.nTech;
  case MUT_RICH: return m.nRich != c.nRich;
  case MUT_NOCAP: return m.nNoCap != c.nNoCap;
  case MUT_ADDON: return m.nAddon != c.nAddon;
  case MUT_NOARMYKEY: return m.nNoArmyKey != c.nNoArmyKey;
  case MUT_ENERGY: return m.nEnergyRegen != c.nEnergyRegen;
  case MUT_NEWBIE: return m.nNewbie != c.nNewbie;
  case MUT_AUTOWORKERS: return m.nAutoWorkers != c.nAutoWorkers;
  case MUT_AUTOMINE: return m.nAutoMine != c.nAutoMine;
  }
  return false;
}

bool MutatorHard(const MutatorSet &m, int nMut) {
  switch (nMut) {
  case MUT_MINERALS: return m.nMinerals != 0 && m.nMinerals < 100;
  case MUT_GAS: return m.nGas != 0 && m.nGas < 100;
  case MUT_WORKERS: return m.nWorkers < 4;
  case MUT_SUPPLY: return m.nSupplyCap != 0 && m.nSupplyCap < 200;
  case MUT_TECH: return m.nTech == MUTATOR_TECH_NO_UPGRADES || m.nTech == MUTATOR_TECH_NO_RESEARCH;
  }
  return false;
}

bool MutatorChoiceHard(int nMut, int nIndex) {
  MutatorSet m;
  MutatorChoose(&m, nMut, nIndex);
  return MutatorHard(m, nMut);
}

const char *MutatorName(int nMut) {
  return nMut >= 0 && nMut < MUT_COUNT ? PortText(kNames[nMut]) : "";
}

const char *MutatorLine(int nMut) {
  return nMut >= 0 && nMut < MUT_COUNT ? PortText(kLines[nMut]) : "";
}

int MutatorPresets(void) {
  return static_cast<int>(sizeof kPresetNames / sizeof kPresetNames[0]);
}

const char *MutatorPresetName(int i) {
  return i >= 0 && i < MutatorPresets() ? PortText(kPresetNames[i]) : PortText("Custom");
}

MutatorSet MutatorPreset(int i) {
  MutatorSet m;
  switch (i) {
  case 1:  // Plenty
    m.nMinerals = m.nGas = 200;
    break;
  case 2:  // Gold Rush, every mineral field and geyser rich as well (the user, 2026-10-25)
    m.nMinerals = m.nGas = 400;
    m.nStartMinerals = 1000;
    m.nStartGas = 500;
    m.nRich = MUTATOR_RICH_EVERY;
    break;
  case 3:  // Infinite
    m.nMinerals = m.nGas = 0;
    break;
  case 4:  // Quick Start
    m.nStartMinerals = 1000;
    m.nStartGas = 500;
    m.nSupplyCap = 400;
    break;
  }
  return m;
}

int MutatorPresetOf(const MutatorSet &m) {
  // the unit limit is Game Options', which no preset sets
  for (int i = 0; i < MutatorPresets(); i++) {
    MutatorSet p = MutatorPreset(i);
    p.nUnits = m.nUnits;
    if (Same(p, m))
      return i;
  }
  return -1;
}

std::string MutatorsText(const MutatorSet &m, bool bShown) {
  const MutatorSet s;
  std::string out;
  // each part in English, or translated when shown; "{0}" in a part is filled with v
  const auto t = [bShown](const char *psz) -> std::string { return bShown ? PortText(psz) : psz; };
  // the parts' separator, which a language may write its own way
  const std::string sep = bShown ? PortTextC("list", ", ") : ", ";
  const auto add = [&out, &t, &sep](const char *psz, std::initializer_list<std::string> v = {}) {
    out += (out.empty() ? "" : sep) + PortTextFill(t(psz).c_str(), v);
  };
  const auto count = [bShown](int n) {
    std::string c = std::to_string(n);
    const std::string comma = bShown ? PortTextC("thousands", ",") : ",";
    for (int i = static_cast<int>(c.size()) - 3; i > 0; i -= 3)
      c.insert(static_cast<size_t>(i), comma);
    return c;
  };
  if (m.nMinerals != s.nMinerals)
    m.nMinerals == 0 ? add(PT_("infinite mineral fields")) : add(PT_("mineral fields {0}"), {Share(m.nMinerals)});
  if (m.nGas != s.nGas)
    m.nGas == 0 ? add(PT_("infinite geysers")) : add(PT_("geysers {0}"), {Share(m.nGas)});
  if (m.nStartMinerals != s.nStartMinerals || m.nStartGas != s.nStartGas)
    // {0} the minerals, {1} the gas
    add(PT_("start resources {0} / {1}"), {count(m.nStartMinerals), count(m.nStartGas)});
  if (m.nSupplyCap != s.nSupplyCap)
    m.nSupplyCap == 0 ? add(PT_("no supply cap")) : add(PT_("supply cap {0}"), {count(m.nSupplyCap)});
  if (m.nWorkers != s.nWorkers) {
    const std::string n = std::to_string(m.nWorkers);
    if (bShown)
      out += (out.empty() ? "" : sep) + PortTextNF("{0} start worker", "{0} start workers", m.nWorkers, {n});
    else
      add(m.nWorkers == 1 ? "{0} start worker" : "{0} start workers", {n});
  }
  if (m.nSupplyBoost != s.nSupplyBoost)
    add(PT_("supply +{0}%"), {std::to_string(m.nSupplyBoost)});
  if (m.nFog == MUTATOR_FOG_EXPLORED)
    add(PT_("the map explored"));
  else if (m.nFog == MUTATOR_FOG_NONE)
    add(PT_("no fog of war"));
  else if (m.nFog == MUTATOR_FOG_RESOURCES)
    add(PT_("the map and its resources explored"));
  if (m.nBuildTime != s.nBuildTime)
    add(PT_("build time {0}"), {Share(m.nBuildTime)});
  if (m.nTech == MUTATOR_TECH_ALL)
    add(PT_("all researched"));
  else if (m.nTech == MUTATOR_TECH_NO_UPGRADES)
    add(PT_("no upgrades"));
  else if (m.nTech == MUTATOR_TECH_NO_RESEARCH)
    add(PT_("no research"));
  if (m.nRich == MUTATOR_RICH_EVERY) {
    add((m.nRichRules & MUTATOR_RICH_QUIET) != 0 ? PT_("every base rich, not announced") : PT_("every base rich"));
  } else if (m.nRich != MUTATOR_RICH_OFF) {
    const std::string chance = std::to_string(m.nRichChance);
    // {0} the chance in percent
    if (m.nRich == MUTATOR_RICH_NATURALS)
      add(PT_("rich resources past the naturals at {0}%"), {chance});
    else if (m.nRich == MUTATOR_RICH_RADIUS)
      // {0} a distance, as "48 tiles" or "half the map"; {1} the chance in percent
      add(PT_("rich resources {0} or more from every start at {1}%"), {RadiusName(m.nRichRadius, bShown), chance});
    else
      add(PT_("rich resources farthest only at {0}%"), {chance});
    if ((m.nRichRules & MUTATOR_RICH_FAIR) == 0)
      add(PT_("each rolled alone"));
    if ((m.nRichRules & MUTATOR_RICH_AT_LEAST_ONE) == 0)
      add(PT_("maybe none"));
    if ((m.nRichRules & MUTATOR_RICH_SAME_MAP) != 0)
      add(PT_("the same every game"));
    if ((m.nRichRules & MUTATOR_RICH_QUIET) != 0)
      add(PT_("not announced"));
  }
  if (m.nUnits != s.nUnits)
    add(PT_("unit limit {0}"), {count(m.nUnits)});
  if (m.nNoCap != s.nNoCap)
    add(PT_("no supply, cheap depots and pylons"));
  if (m.nAddon != s.nAddon)
    add(PT_("one add-on serves every building"));
  if (m.nNoArmyKey != s.nNoArmyKey)
    add(PT_("no army hotkey"));
  if (m.nEnergyRegen != s.nEnergyRegen)
    add(PT_("energy x3"));
  if (m.nNewbie != s.nNewbie)
    add(PT_("newbie helpers"));
  if (m.nAutoWorkers != s.nAutoWorkers)
    add(PT_("workers trained by themselves"));
  if (m.nAutoMine != s.nAutoMine)
    add(PT_("mining balanced by itself"));
  return out;
}

std::string MutatorsJson(const MutatorSet &m) {
  return "{\"minerals\":" + std::to_string(m.nMinerals) + ",\"gas\":" + std::to_string(m.nGas) +
         ",\"startMinerals\":" + std::to_string(m.nStartMinerals) + ",\"startGas\":" + std::to_string(m.nStartGas) +
         ",\"supply\":" + std::to_string(m.nSupplyCap) + ",\"workers\":" + std::to_string(m.nWorkers) +
         ",\"boost\":" + std::to_string(m.nSupplyBoost) + ",\"fog\":" + std::to_string(m.nFog) +
         ",\"build\":" + std::to_string(m.nBuildTime) + ",\"tech\":" + std::to_string(m.nTech) +
         ",\"rich\":" + std::to_string(m.nRich) + ",\"richChance\":" + std::to_string(m.nRichChance) +
         ",\"richRules\":" + std::to_string(m.nRichRules) + ",\"richRadius\":" + std::to_string(m.nRichRadius) +
         ",\"units\":" + std::to_string(m.nUnits) + ",\"noCap\":" + std::to_string(m.nNoCap) +
         ",\"addon\":" + std::to_string(m.nAddon) + ",\"noArmyKey\":" + std::to_string(m.nNoArmyKey) +
         ",\"energyRegen\":" + std::to_string(m.nEnergyRegen) + ",\"newbie\":" + std::to_string(m.nNewbie) +
         ",\"autoWorkers\":" + std::to_string(m.nAutoWorkers) + ",\"autoMine\":" + std::to_string(m.nAutoMine) +
         ",\"autoUpgLo\":" + std::to_string(m.dwAutoUpgLo) + ",\"autoUpgHi\":" + std::to_string(m.dwAutoUpgHi) + "}";
}

MutatorSet MutatorsFromJson(const PortJson &j) {
  const MutatorSet s;
  MutatorSet m;
  m.nMinerals = static_cast<int>(j["minerals"].Num(s.nMinerals));
  m.nGas = static_cast<int>(j["gas"].Num(s.nGas));
  m.nStartMinerals = static_cast<int>(j["startMinerals"].Num(s.nStartMinerals));
  m.nStartGas = static_cast<int>(j["startGas"].Num(s.nStartGas));
  m.nSupplyCap = static_cast<int>(j["supply"].Num(s.nSupplyCap));
  m.nWorkers = static_cast<int>(j["workers"].Num(s.nWorkers));
  m.nSupplyBoost = static_cast<int>(j["boost"].Num(s.nSupplyBoost));
  m.nFog = static_cast<int>(j["fog"].Num(s.nFog));
  m.nBuildTime = static_cast<int>(j["build"].Num(s.nBuildTime));
  m.nTech = static_cast<int>(j["tech"].Num(s.nTech));
  m.nRich = static_cast<int>(j["rich"].Num(s.nRich));
  m.nRichChance = static_cast<int>(j["richChance"].Num(s.nRichChance));
  m.nRichRules = static_cast<int>(j["richRules"].Num(s.nRichRules));
  m.nRichRadius = static_cast<int>(j["richRadius"].Num(s.nRichRadius));
  m.nUnits = static_cast<int>(j["units"].Num(s.nUnits));
  m.nNoCap = static_cast<int>(j["noCap"].Num(s.nNoCap));
  m.nAddon = static_cast<int>(j["addon"].Num(s.nAddon));
  m.nNoArmyKey = static_cast<int>(j["noArmyKey"].Num(s.nNoArmyKey));
  m.nEnergyRegen = static_cast<int>(j["energyRegen"].Num(s.nEnergyRegen));
  m.nNewbie = static_cast<int>(j["newbie"].Num(s.nNewbie));
  m.nAutoWorkers = static_cast<int>(j["autoWorkers"].Num(s.nAutoWorkers));
  m.nAutoMine = static_cast<int>(j["autoMine"].Num(s.nAutoMine));
  m.dwAutoUpgLo = static_cast<unsigned>(j["autoUpgLo"].Num(s.dwAutoUpgLo));
  m.dwAutoUpgHi = static_cast<unsigned>(j["autoUpgHi"].Num(s.dwAutoUpgHi));
  return MutatorsClamp(m);
}

void MutatorsChoose(const MutatorSet &m) {
  Session().mut = MutatorsClamp(m);
  Session().bMutators = true;
}

bool MutatorsChooseList(const char *pszList) {
  if (pszList == NULL || *pszList == '\0')
    return false;
  for (int i = 0; i < MutatorPresets(); i++)
    if (Bare(pszList) == Bare(kPresetNames[i]) || (i == 1 && Bare(pszList) == "rich")) {
      MutatorsChoose(MutatorPreset(i));
      Log("--mutators %s: %s", pszList, i == 0 ? "classic" : MutatorsText(Session().mut).c_str());
      return true;
    }
  MutatorSet m;
  bool bAny = false;
  std::string list = pszList;
  size_t at = 0;
  while (at <= list.size()) {
    size_t end = list.find(',', at);
    if (end == std::string::npos)
      end = list.size();
    const std::string item = list.substr(at, end - at);
    at = end + 1;
    const size_t eq = item.find_first_of("=:");
    if (eq == std::string::npos)
      continue;
    const std::string key = item.substr(0, eq);
    const char *pszValue = item.c_str() + eq + 1;
    if (lstrcmpiA(key.c_str(), "minerals") == 0) {
      m.nMinerals = ParseShare(pszValue);
    } else if (lstrcmpiA(key.c_str(), "gas") == 0) {
      m.nGas = ParseShare(pszValue);
    } else if (lstrcmpiA(key.c_str(), "start") == 0) {
      m.nStartMinerals = atoi(pszValue);
      const char *pSlash = strchr(pszValue, '/');
      m.nStartGas = pSlash != NULL ? atoi(pSlash + 1) : 0;
    } else if (lstrcmpiA(key.c_str(), "supply") == 0 || lstrcmpiA(key.c_str(), "cap") == 0) {
      m.nSupplyCap = _strnicmp(pszValue, "unl", 3) == 0 || _strnicmp(pszValue, "none", 4) == 0 ? 0 : atoi(pszValue);
    } else if (lstrcmpiA(key.c_str(), "workers") == 0) {
      m.nWorkers = atoi(pszValue);
    } else if (lstrcmpiA(key.c_str(), "boost") == 0) {
      m.nSupplyBoost = atoi(pszValue);
    } else if (lstrcmpiA(key.c_str(), "fog") == 0) {
      m.nFog = ParseFog(pszValue);
    } else if (lstrcmpiA(key.c_str(), "build") == 0 || lstrcmpiA(key.c_str(), "time") == 0) {
      m.nBuildTime = ParseShare(pszValue);
    } else if (lstrcmpiA(key.c_str(), "tech") == 0) {
      m.nTech = ParseTech(pszValue);
    } else if (lstrcmpiA(key.c_str(), "rich") == 0) {
      m.nRich = ParseRich(pszValue);
    } else if (lstrcmpiA(key.c_str(), "chance") == 0) {
      m.nRichChance = atoi(pszValue);
    } else if (lstrcmpiA(key.c_str(), "radius") == 0 || lstrcmpiA(key.c_str(), "distance") == 0) {
      m.nRichRadius = ParseRadius(pszValue);
    } else if (lstrcmpiA(key.c_str(), "fair") == 0) {
      m.nRichRules = ParseOn(pszValue) ? m.nRichRules | MUTATOR_RICH_FAIR : m.nRichRules & ~MUTATOR_RICH_FAIR;
    } else if (lstrcmpiA(key.c_str(), "atleast") == 0) {
      m.nRichRules = ParseOn(pszValue) ? m.nRichRules | MUTATOR_RICH_AT_LEAST_ONE
                                       : m.nRichRules & ~MUTATOR_RICH_AT_LEAST_ONE;
    } else if (lstrcmpiA(key.c_str(), "announce") == 0) {
      m.nRichRules = ParseOn(pszValue) ? m.nRichRules & ~MUTATOR_RICH_QUIET : m.nRichRules | MUTATOR_RICH_QUIET;
    } else if (lstrcmpiA(key.c_str(), "units") == 0 || lstrcmpiA(key.c_str(), "unitlimit") == 0) {
      m.nUnits = _strnicmp(pszValue, "auto", 4) == 0 ? 0 : atoi(pszValue);
    } else if (lstrcmpiA(key.c_str(), "richseed") == 0) {
      m.nRichRules = _strnicmp(pszValue, "map", 3) == 0 ? m.nRichRules | MUTATOR_RICH_SAME_MAP
                                                         : m.nRichRules & ~MUTATOR_RICH_SAME_MAP;
    } else if (lstrcmpiA(key.c_str(), "nocap") == 0 || lstrcmpiA(key.c_str(), "nosupply") == 0) {
      m.nNoCap = ParseOn(pszValue) ? 1 : 0;
    } else if (lstrcmpiA(key.c_str(), "noarmy") == 0 || lstrcmpiA(key.c_str(), "noarmykey") == 0) {
      m.nNoArmyKey = ParseOn(pszValue) ? 1 : 0;
    } else if (lstrcmpiA(key.c_str(), "energy") == 0 || lstrcmpiA(key.c_str(), "energyregen") == 0) {
      m.nEnergyRegen = ParseOn(pszValue) ? 1 : 0;
    } else if (lstrcmpiA(key.c_str(), "newbie") == 0) {
      m.nNewbie = ParseOn(pszValue) ? 1 : 0;
    } else if (lstrcmpiA(key.c_str(), "autoworkers") == 0) {
      m.nAutoWorkers = ParseOn(pszValue) ? 1 : 0;
    } else if (lstrcmpiA(key.c_str(), "automine") == 0) {
      m.nAutoMine = ParseOn(pszValue) ? 1 : 0;
    } else if (lstrcmpiA(key.c_str(), "autoupg") == 0) {
      // the upgrades to research by themselves, by id or by a '+'-joined list of ids
      const char *p = pszValue;
      while (*p != ' ') {
        const int u = atoi(p);
        if (u >= 0 && u < 32)
          m.dwAutoUpgLo |= 1u << u;
        else if (u >= 32 && u < 61)
          m.dwAutoUpgHi |= 1u << (u - 32);
        const char *pNext = strchr(p, '+');
        if (pNext == NULL)
          break;
        p = pNext + 1;
      }
    } else if (lstrcmpiA(key.c_str(), "addon") == 0 || lstrcmpiA(key.c_str(), "oneaddon") == 0) {
      m.nAddon = ParseOn(pszValue) ? 1 : 0;
    } else {
      Log("--mutators: %s is none of minerals, gas, start, supply, workers, boost, fog, build, tech, rich, radius, "
          "chance, fair, atleast, announce, richseed, nocap, addon and units", key.c_str());
      continue;
    }
    bAny = true;
  }
  if (bAny) {
    MutatorsChoose(m);
    Log("--mutators %s: %s", pszList, MutatorsStandard(Session().mut) ? "classic" : MutatorsText(Session().mut).c_str());
  }
  return bAny;
}

std::string MutatorsList(const MutatorSet &m) {
  const MutatorSet c;
  std::string s;
  const auto add = [&s](const char *pszKey, const std::string &value) {
    s += (s.empty() ? "" : ",") + std::string(pszKey) + "=" + value;
  };
  const auto share = [](int n) { return n == 0 ? std::string("endless") : std::to_string(n) + "%"; };
  if (m.nMinerals != c.nMinerals)
    add("minerals", share(m.nMinerals));
  if (m.nGas != c.nGas)
    add("gas", share(m.nGas));
  if (m.nStartMinerals != c.nStartMinerals || m.nStartGas != c.nStartGas)
    add("start", std::to_string(m.nStartMinerals) + "/" + std::to_string(m.nStartGas));
  if (m.nSupplyCap != c.nSupplyCap)
    add("supply", m.nSupplyCap == 0 ? std::string("unlimited") : std::to_string(m.nSupplyCap));
  if (m.nWorkers != c.nWorkers)
    add("workers", std::to_string(m.nWorkers));
  if (m.nSupplyBoost != c.nSupplyBoost)
    add("boost", std::to_string(m.nSupplyBoost));
  if (m.nFog != c.nFog)
    add("fog", m.nFog == MUTATOR_FOG_EXPLORED ? std::string("explored")
                 : m.nFog == MUTATOR_FOG_NONE   ? std::string("none")
                                                : std::to_string(m.nFog));
  if (m.nBuildTime != c.nBuildTime)
    add("build", std::to_string(m.nBuildTime) + "%");
  if (m.nTech != c.nTech)
    add("tech", m.nTech == MUTATOR_TECH_ALL           ? std::string("all")
                  : m.nTech == MUTATOR_TECH_NO_UPGRADES ? std::string("noupgrades")
                  : m.nTech == MUTATOR_TECH_NO_RESEARCH ? std::string("noresearch")
                                                        : std::to_string(m.nTech));
  if (m.nRich != c.nRich) {
    static const char *const kRich[] = {"off", "naturals", "radius", "far", "every"};
    add("rich", m.nRich >= 0 && m.nRich <= MUTATOR_RICH_EVERY ? std::string(kRich[m.nRich]) : std::to_string(m.nRich));
    if (m.nRichChance != c.nRichChance)
      add("chance", std::to_string(m.nRichChance));
    if (m.nRichRadius != c.nRichRadius)
      add("radius", m.nRichRadius < 0 ? std::to_string(-m.nRichRadius) + "%" : std::to_string(m.nRichRadius));
    if ((m.nRichRules & MUTATOR_RICH_FAIR) == 0)
      add("fair", "off");
    if ((m.nRichRules & MUTATOR_RICH_AT_LEAST_ONE) == 0)
      add("atleast", "off");
    if ((m.nRichRules & MUTATOR_RICH_SAME_MAP) != 0)
      add("richseed", "map");
    if ((m.nRichRules & MUTATOR_RICH_QUIET) != 0)
      add("announce", "off");
  }
  if (m.nUnits != c.nUnits)
    add("units", std::to_string(m.nUnits));
  if (m.nNoCap != c.nNoCap)
    add("nocap", "on");
  if (m.nAddon != c.nAddon)
    add("addon", "on");
  if (m.nNoArmyKey != c.nNoArmyKey)
    add("noarmy", "on");
  if (m.nEnergyRegen != c.nEnergyRegen)
    add("energy", "on");
  if (m.nNewbie != c.nNewbie)
    add("newbie", "on");
  if (m.nAutoWorkers != c.nAutoWorkers)
    add("autoworkers", "on");
  if (m.nAutoMine != c.nAutoMine)
    add("automine", "on");
  if (m.dwAutoUpgLo != 0 || m.dwAutoUpgHi != 0) {
    // the upgrades to research by themselves, as a '+'-joined list of ids (the parser reads it back)
    std::string ids;
    for (int u = 0; u < 61; ++u) {
      const bool bOn = u < 32 ? (m.dwAutoUpgLo & (1u << u)) != 0 : (m.dwAutoUpgHi & (1u << (u - 32))) != 0;
      if (bOn)
        ids += (ids.empty() ? "" : "+") + std::to_string(u);
    }
    add("autoupg", ids);
  }
  return s;
}

void MutatorsChooseNet(const MutatorSet &m) {
  g_bNet = true;
  g_net = MutatorsClamp(m);
}

// The SB relay launch's --mutators, the same list on every seat: MutatorsChooseList
// parses the string (into the single-player session's set), and the network game
// reads the net set, so the parsed values are copied there.
bool MutatorsChooseNetList(const char *pszList) {
  if (!MutatorsChooseList(pszList))
    return false;
  MutatorsChooseNet(Session().mut);
  Log("the net mutators chosen: %s", MutatorsText(Session().mut).c_str());
  return true;
}

bool MutatorsChosenThisGame(void) {
  if (NetChosen())
    return !MutatorsStandard(g_net);
  return Session().bMutators && !MutatorsStandard(Session().mut);
}

void MutatorsForGame(void) {
  const bool bOld = gPlayback || gpLoadGameFile != NULL;
  g_game = MutatorSet();
  if ((g_hxRuleOptions & HX_OPT_MUTATORS) != 0) {
    if (bOld)
      g_game = g_bRecorded ? g_recorded : MutatorSet();
    else if (NetChosen())
      g_game = g_net;
    else if (Session().bMutators)
      g_game = Session().mut;
  }
  g_nRevision = bOld ? (g_bRecorded ? g_nRecordedRevision : 0) : MUTATOR_REVISION;
  g_bNet = false;
  g_hxMutatorsOn = !MutatorsStandard(g_game);
  Log("MutatorsForGame: on %d, options 0x%X, set %s", static_cast<int>(g_hxMutatorsOn),
      static_cast<unsigned>(g_hxRuleOptions), MutatorsText(g_game).c_str());
  if ((g_hxRuleOptions & HX_OPT_MUTATORS) != 0)
    Log("a game %s: %s", bOld ? "played back or loaded" : "new",
        g_hxMutatorsOn ? MutatorsText(g_game).c_str() : "none other than classic");
}

const MutatorSet &MutatorsOfGame(void) {
  return g_game;
}

unsigned short MutatorResourceOf(unsigned short wAmount, bool bGas) {
  const int nShare = bGas ? g_game.nGas : g_game.nMinerals;
  if (nShare == 0 || nShare == 100 || wAmount == 0)
    return wAmount;  // an endless one keeps the map's, which the console shows
  const long long ll = (static_cast<long long>(wAmount) * nShare + 50) / 100;
  return static_cast<unsigned short>(ll < 1 ? 1 : ll > 0xFFFF ? 0xFFFF : ll);
}

bool MutatorEndlessOf(bool bGas) {
  return (bGas ? g_game.nGas : g_game.nMinerals) == 0;
}

int MutatorFoodMadeOf(int nMade) {
  const int nBoosted = nMade * (100 + g_game.nSupplyBoost) / 100;
  // whole supply (revision 1): a town hall's 10 at +25% gives 12, not 12.5, which the console
  // showed as 13 (the user, 2026-10-24: "Whole supply only"); a mod's supplier of a half keeps it
  if (g_nRevision < 1 || g_game.nSupplyBoost == 0)
    return nBoosted;
  const int nWhole = nBoosted & ~1;
  return nWhole > nMade ? nWhole : nMade;
}

int MutatorFoodUsedOf(int nUsed) {
  return g_game.nNoCap ? 0 : nUsed;
}

bool MutatorAddonOf(void) {
  return g_game.nAddon != 0;
}

int MutatorSupplyCapOf(void) {
  return g_game.nSupplyCap == 0 || g_game.nNoCap ? kUnlimitedHalves : g_game.nSupplyCap * 2;
}

int MutatorWorkersOf(void) {
  return g_game.nWorkers;
}

bool MutatorStartResources(int *pnMinerals, int *pnGas) {
  const MutatorSet s;
  if (!g_hxMutatorsOn || (g_game.nStartMinerals == s.nStartMinerals && g_game.nStartGas == s.nStartGas))
    return false;
  *pnMinerals = g_game.nStartMinerals;
  *pnGas = g_game.nStartGas;
  return true;
}

int MutatorTechOf(void) {
  return g_game.nTech;
}

void MutatorsMapLoaded(void) {
  RichBasesMapLoaded();
  if (!g_hxMutatorsOn)
    return;
  const bool bLoaded = gpLoadGameFile != NULL;
  if (g_game.nBuildTime != 100) {
    // a save gives 1.17's rows back as they were saved, at the mutator's times; a mod's unit
    // types are the mod's again at each start
    for (unsigned t = 0; UnitTypeValid(t); t = UnitTypeNext(t))
      if (!bLoaded || UnitTypeMod(t))
        guwUnitTimeToBuild[t] = Timed(guwUnitTimeToBuild[t]);
    // and their techs and upgrades, past 1.17's rows
    for (unsigned u = 0; u < static_cast<unsigned>(g_hxTypes.upgrades); u++)
      if (UpgradeTypeValid(u) && (!bLoaded || UpgradeTypeMod(u))) {
        upg_timeCostBase_g[u] = Timed(upg_timeCostBase_g[u]);
        upg_timeCostFactor_g[u] = Timed(upg_timeCostFactor_g[u]);
      }
    for (unsigned t = 0; t < static_cast<unsigned>(g_hxTypes.techs); t++)
      if (TechTypeValid(t) && (!bLoaded || TechTypeMod(t)))
        techdata_timeCost_g[t] = Timed(techdata_timeCost_g[t]);
  }
  if (!bLoaded && g_game.nTech == MUTATOR_TECH_ALL) {
    for (int p = 0; p < PlayerSlots(); p++) {
      for (int t = 0; t < g_hxTypes.techs; t++)  // a mod's past 1.17's rows too
        if (TechTypeValid(t) && get_tech_available(p, static_cast<WORD>(t)))
          set_tech_researched(p, static_cast<WORD>(t), 1);
      for (int u = 0; u < g_hxTypes.upgrades; u++) {
        if (!UpgradeTypeValid(u))
          continue;
        const BYTE bMax = get_max_upgrade_level(p, static_cast<WORD>(u));
        if (get_upgrade_level(p, static_cast<WORD>(u)) >= bMax)
          continue;
        set_upgrade_level(p, static_cast<WORD>(u), bMax);
        // the units a map placed before the start units, as the upgrade cheat gives them
        // theirs (cheat_set_bits); the start units take theirs as they are made
        CUnit *pUnit = CUnit::GetPlayerUnitList(p);
        if (pUnit != NULL)
          pUnit->sub_43B0B0(u);
      }
    }
  }
  if (!bLoaded && g_game.nNoCap) {
    // the no-cap mutator (the user, 2026-11-02): the supply buildings are built for their
    // tech, not their supply, so they cost 40 minerals and hold half their life, a pylon's
    // half its shields. The tables hold what every reader and the save then have, as the
    // build times above: a save carries the changed values in its own table copies, which
    // the load puts back, so only a new game changes them here.
    for (const int t : kNoCapTypes) {
      guwUnitMineralsCost[t] = kNoCapMinerals;
      gxUnitLife[t] = static_cast<LONG>(gxUnitLife[t] / 2);
      if (gbUnitHasShields[t])
        guwUnitEnergy[t] = static_cast<WORD>(guwUnitEnergy[t] / 2);
    }
    Log("no cap: the depot is %d minerals and %d life; the pylon %d minerals, %d life and %d shields",
        guwUnitMineralsCost[kNoCapTypes[0]], static_cast<int>(gxUnitLife[kNoCapTypes[0]] >> 8),
        guwUnitMineralsCost[kNoCapTypes[1]], static_cast<int>(gxUnitLife[kNoCapTypes[1]] >> 8),
        guwUnitEnergy[kNoCapTypes[1]]);
  }
  if (!bLoaded)
    MutatorsVisionRefresh();
  Log("the map loaded%s: times at %d%%, tech %d, fog %d", bLoaded ? " for a saved game" : "", g_game.nBuildTime,
      g_game.nTech, g_game.nFog);
}

static void RevealTilesFor(PlayerSet *pHidden, PlayerSet *pUnexplored, int nX, int nY);

void MutatorsVisionRefresh(void) {
  if (!g_hxMutatorsOn || g_game.nFog == MUTATOR_FOG_CLASSIC || gpSquareMap == NULL)
    return;
  // a tile's bits 0 to 7 hide it from players 0 to 7, bits 8 to 15 leave it unexplored.
  // Explored + resources does NOT open the land: the fog of war stays as Classic, and only
  // the resources, the eggs and the doodads show through it (the user, 2026-11-04)
  const bool bSeen = g_game.nFog == MUTATOR_FOG_NONE;
  const DWORD dwOpen = bSeen ? 0xFFFFu : g_game.nFog == MUTATOR_FOG_RESOURCES ? 0u : 0xFF00u;
  const int nTiles = static_cast<int>(gwMapWidth) * gwMapHeight;
  for (int i = 0; i < nTiles; i++)
    gpSquareMap[i] &= ~dwOpen;
  PlayerSet *pHidden = VisionHxHidden();
  PlayerSet *pUnexplored = VisionHxUnexplored();
  for (int s = PLAYER_LOBBY_117; s < PlayerSlots(); s++)
    for (int i = 0; i < nTiles; i++) {
      // Explored and None open the extra slots' unexplored bits; Explored + resources does
      // not, so the land stays fogged there too (the user, 2026-11-04)
      if ((bSeen || g_game.nFog == MUTATOR_FOG_EXPLORED) && pUnexplored != NULL)
        pUnexplored[i].Remove(s);
      if (bSeen && pHidden != NULL)
        pHidden[i].Remove(s);
    }
  // Explored + resources (the user, 2026-11-04): the land is known, as Explored, and the
  // mineral fields, the geysers, the eggs and the doodads show for every player from the
  // start. The resource tiles open for every player, so the resources themselves stay
  // visible (CalcVisibility keeps them so); the rest of the map's units stay in the fog.
  if (g_game.nFog == MUTATOR_FOG_RESOURCES) {
    for (CUnit *p = CUnit::GetUsedList(); p != NULL; p = p->pNext) {
      if (p->pSprite == NULL)
        continue;
      const WORD wType = p->wType;
      if ((wType >= 0xB0 && wType <= 0xB2) || wType == 0xBC || wType == 0x24)
        RevealTilesFor(pHidden, pUnexplored, static_cast<int>(p->pSprite->wPosX), static_cast<int>(p->pSprite->wPosY));
    }
    // the doodads: the rocks and the other obstacles, in the thingy pool
    for (int n = 1; n <= 500; ++n) {
      CThingy *pT = CThingy::GetDoodadThingy(n);
      if (pT == NULL || pT->pSprite == NULL)
        continue;
      RevealTilesFor(pHidden, pUnexplored, static_cast<int>(pT->pSprite->wPosX), static_cast<int>(pT->pSprite->wPosY));
    }
  }
}

// Clear a unit's tiles' hidden and unexplored bits for one player: a small area round its
// centre, enough for its sprite to show. The classic players' bits live in gpSquareMap
// (bits 0-7), the Hyperion slots past 1.17's eight in the vision tables.
static void RevealTilesFor(PlayerSet *pHidden, PlayerSet *pUnexplored, int nX, int nY) {
  const int nTx = nX / 32, nTy = nY / 32;
  for (int dy = -1; dy <= 1; ++dy)
    for (int dx = -1; dx <= 1; ++dx) {
      const int x = nTx + dx, y = nTy + dy;
      if (x < 0 || y < 0 || x >= gwMapWidth || y >= gwMapHeight)
        continue;
      const int i = y * gwMapWidth + x;
      // every player's hidden and unexplored bits on the square map, not just the extra
      // slots': the classic eight are bits 0 to 7, so 0x00FF, and the unexplored ones are
      // bits 8 to 15, so 0xFF00; the Hyperion slots past them have their own tables
      gpSquareMap[i] &= ~0xFFFFu;
      for (int s = PLAYER_LOBBY_117; s < PlayerSlots(); ++s) {
        if (pHidden != NULL)
          pHidden[i].Remove(s);
        if (pUnexplored != NULL)
          pUnexplored[i].Remove(s);
      }
    }
}

void MutatorsSave(unsigned char *pbOut) {
  const Record r = {static_cast<DWORD>(g_game.nMinerals),      static_cast<DWORD>(g_game.nGas),
                    static_cast<DWORD>(g_game.nStartMinerals), static_cast<DWORD>(g_game.nStartGas),
                    static_cast<DWORD>(g_game.nSupplyCap),     static_cast<DWORD>(g_game.nWorkers),
                    static_cast<DWORD>(g_game.nSupplyBoost),   static_cast<DWORD>(g_game.nFog),
                    static_cast<DWORD>(g_game.nBuildTime),     static_cast<DWORD>(g_game.nTech),
                    static_cast<DWORD>(g_nRevision),           static_cast<DWORD>(g_game.nRich),
                    static_cast<DWORD>(g_game.nRichChance),    static_cast<DWORD>(g_game.nRichRules),
                    static_cast<DWORD>(g_game.nRichRadius),    static_cast<DWORD>(g_game.nUnits),
                    static_cast<DWORD>(g_game.nNoCap),         static_cast<DWORD>(g_game.nAddon),
                    static_cast<DWORD>(g_game.nNoArmyKey),     static_cast<DWORD>(g_game.nEnergyRegen),
                    static_cast<DWORD>(g_game.nNewbie),        static_cast<DWORD>(g_game.nAutoWorkers),
                    static_cast<DWORD>(g_game.nAutoMine),      static_cast<DWORD>(g_game.dwAutoUpgLo),
                    static_cast<DWORD>(g_game.dwAutoUpgHi)};
  memcpy(pbOut, &r, sizeof r);
}

bool MutatorRecordBytesRead(int nBytes) {
  return nBytes == MUTATOR_RECORD_BYTES_NEWBIE || nBytes == MUTATOR_RECORD_BYTES || nBytes == MUTATOR_RECORD_BYTES_UNITS ||
         nBytes == MUTATOR_RECORD_BYTES_RADIUS || nBytes == MUTATOR_RECORD_BYTES_RICH ||
         nBytes == MUTATOR_RECORD_BYTES_REVISION ||
         nBytes == MUTATOR_RECORD_BYTES_TEN || nBytes == MUTATOR_RECORD_BYTES_FIRST;
}

bool MutatorsRecorded(const unsigned char *pbIn, int nBytes) {
  g_bRecorded = false;
  g_recorded = MutatorSet();
  g_nRecordedRevision = 0;
  if (pbIn == NULL)
    return true;
  if (!MutatorRecordBytesRead(nBytes))
    return false;
  // a record without the revision is 0.6.9's, revision 0; one without rich bases has none, one
  // without the radius 48 tiles, one without the unit limit Automatic, and one without the no-cap
  // mutator and the one add-on's has neither
  Record r = {0, 0, 0, 0, 0, 0, 0, MUTATOR_FOG_CLASSIC, 100, MUTATOR_TECH_CLASSIC, 0, MUTATOR_RICH_OFF, 50,
              MUTATOR_RICH_FAIR | MUTATOR_RICH_AT_LEAST_ONE, 48, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
  memcpy(&r, pbIn, nBytes);
  g_nRecordedRevision = static_cast<int>(r.dwRevision);
  MutatorSet m;
  m.nMinerals = static_cast<int>(r.dwMinerals);
  m.nGas = static_cast<int>(r.dwGas);
  m.nStartMinerals = static_cast<int>(r.dwStartMinerals);
  m.nStartGas = static_cast<int>(r.dwStartGas);
  m.nSupplyCap = static_cast<int>(r.dwSupplyCap);
  m.nWorkers = static_cast<int>(r.dwWorkers);
  m.nSupplyBoost = static_cast<int>(r.dwSupplyBoost);
  m.nFog = static_cast<int>(r.dwFog);
  m.nBuildTime = static_cast<int>(r.dwBuildTime);
  m.nTech = static_cast<int>(r.dwTech);
  m.nRich = static_cast<int>(r.dwRich);
  m.nRichChance = static_cast<int>(r.dwRichChance);
  m.nRichRules = static_cast<int>(r.dwRichRules);
  m.nRichRadius = static_cast<int>(r.dwRichRadius);
  m.nUnits = static_cast<int>(r.dwUnits);
  m.nNoCap = static_cast<int>(r.dwNoCap);
  m.nAddon = static_cast<int>(r.dwAddon);
  m.nNoArmyKey = static_cast<int>(r.dwNoArmyKey);
  m.nEnergyRegen = static_cast<int>(r.dwEnergyRegen);
  m.nNewbie = static_cast<int>(r.dwNewbie);
  m.nAutoWorkers = static_cast<int>(r.dwAutoWorkers);
  m.nAutoMine = static_cast<int>(r.dwAutoMine);
  m.dwAutoUpgLo = r.dwAutoUpgLo;
  m.dwAutoUpgHi = r.dwAutoUpgHi;
  g_recorded = MutatorsClamp(m);
  g_bRecorded = true;
  return true;
}
