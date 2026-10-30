// port_handicap.cpp -- a player's handicap (h/handicap.h; port_handicap.h).
//
// StarCraft II's lobby option as it plays: a share of every unit's and building's maximum life
// and shields, 50% to 100% in steps of 10. The game asks for a maximum with the unit's owner
// (HandicapLife, HandicapShields), which this file answers from the slot's percent; a game
// without a handicap never gets here (g_hxHandicapOn).
#include <windows.h>
#include <stdlib.h>
#include <string.h>

#include "port_handicap.h"
#include "port_session.h"  // Session
#include "../platform/port_log.h"  // PortLogger
#include "../../h/handicap.h"
#include "../../h/hxrules.h"       // HX_OPT_HANDICAP, g_hxRuleOptions
#include "../../h/playerlimits.h"  // PLAYER_SLOTS_MAX, PLAYER_NEUTRAL
#include "../../h/playertables.h"  // PlayersSeatedComputer, PlayersCameFrom
#include "../../h/net_cmds.h"      // PlayerData
#include "../../h/CUnit.h"
#include "../../gen/globals117.h"  // gPlayerData, g_LocalNationID, gxUnitLife, guwUnitEnergy

bool g_hxHandicapOn = false;

namespace {

const PortLogger Log("hyperion_handicap.log");

const int kChoices[] = {100, 90, 80, 70, 60, 50};

BYTE g_abSlot[PLAYER_SLOTS_MAX];      // each slot's percent in the game being played
BYTE g_abRecorded[PLAYER_SLOTS_MAX];  // the last replay's or save's record
bool g_bRecorded;                     // and whether it had one
// a room's match: each person's by name, each computer's in the launch's order
bool g_bNet;
std::vector<std::pair<std::string, int>> g_netPeople;
std::vector<int> g_netComputers;

// The room's choice is for a game over the network; a launch that failed before its game
// leaves it unread, and a game in single player then takes the session's.
bool NetChosen() {
  return g_bNet && gbMultiPlayerMode != 0;
}

int Percent(int nPlayer) {
  return nPlayer >= 0 && nPlayer < PLAYER_SLOTS_MAX && g_abSlot[nPlayer] != 0 ? g_abSlot[nPlayer] : HANDICAP_FULL;
}

// The k of a computer in slot s among the game's computers, by the lobby slot each came from
// before the shuffle of the starts: 1.17's set-up and a room's lobby both fill the open slots
// in order, so the k-th computer in that order is the k-th one chosen. -1 for none.
int ComputerOrder(int s) {
  if (gPlayerData[s].nType != 1)
    return -1;
  const int nFrom = PlayersCameFrom(s);
  int k = 0;
  for (int t = 0; t < PLAYER_SLOTS_MAX; t++)
    if (t != s && gPlayerData[t].nType == 1 && PlayersCameFrom(t) < nFrom)
      ++k;
  return k;
}

// The session's row of slot s in a new game: the local player row 0, the k-th computer
// seated 1 + k (port_ai.cpp's mapping); -1 for neither.
int RowOf(int s) {
  if (s == static_cast<int>(g_LocalNationID))
    return 0;
  int k = PlayersSeatedComputer(s);
  if (k < 0)
    k = ComputerOrder(s);
  return k >= 0 && k < PLAYER_SLOTS_MAX ? 1 + k : -1;
}

int NetPercent(int s) {
  if (gPlayerData[s].nType == 2) {
    for (const auto &p : g_netPeople)
      if (p.first.substr(0, 24) == gPlayerData[s].szName)
        return p.second;
    return HANDICAP_FULL;
  }
  const int k = ComputerOrder(s);
  return k >= 0 && k < static_cast<int>(g_netComputers.size()) ? g_netComputers[k] : HANDICAP_FULL;
}

}  // namespace

int HandicapChoices() {
  return static_cast<int>(sizeof kChoices / sizeof kChoices[0]);
}

int HandicapChoice(int nIndex) {
  return nIndex >= 0 && nIndex < HandicapChoices() ? kChoices[nIndex] : HANDICAP_FULL;
}

int HandicapClamp(int nPercent) {
  if (nPercent >= HANDICAP_FULL)
    return HANDICAP_FULL;
  if (nPercent <= HANDICAP_LEAST)
    return HANDICAP_LEAST;
  return (nPercent + HANDICAP_STEP / 2) / HANDICAP_STEP * HANDICAP_STEP;
}

void HandicapChoose(int nRow, int nPercent) {
  if (nRow < 0 || nRow > PLAYER_SLOTS_MAX)
    return;
  Session().aRow[nRow].bHandicap = static_cast<unsigned char>(HandicapClamp(nPercent));
  Session().bHandicaps = true;
}

void HandicapChooseList(const char *pszList) {
  int nRow = 0;
  for (const char *p = pszList; p != NULL && *p != '\0' && nRow <= PLAYER_SLOTS_MAX;) {
    if (*p == ',' || *p == ' ') {
      ++p;
      continue;
    }
    char *pEnd = NULL;
    const long n = strtol(p, &pEnd, 10);
    if (pEnd == p)
      break;
    HandicapChoose(nRow++, static_cast<int>(n));
    p = pEnd;
    if (*p == '%')
      ++p;
  }
}

void HandicapChooseNet(const std::vector<std::pair<std::string, int>> &people, const std::vector<int> &computers) {
  g_bNet = true;
  g_netPeople = people;
  for (auto &p : g_netPeople)
    p.second = HandicapClamp(p.second);
  g_netComputers.clear();
  for (int n : computers)
    g_netComputers.push_back(HandicapClamp(n));
}

bool HandicapChosenThisGame() {
  if (NetChosen()) {
    for (const auto &p : g_netPeople)
      if (p.second < HANDICAP_FULL)
        return true;
    for (int n : g_netComputers)
      if (n < HANDICAP_FULL)
        return true;
    return false;
  }
  if (!Session().bHandicaps)
    return false;
  for (int r = 0; r <= PLAYER_SLOTS_MAX; r++)
    if (Session().aRow[r].bHandicap < HANDICAP_FULL)
      return true;
  return false;
}

void HandicapForGame() {
  const bool bOld = gPlayback || gpLoadGameFile != NULL;
  for (int s = 0; s < PLAYER_SLOTS_MAX; s++) {
    g_abSlot[s] = HANDICAP_FULL;
    if ((g_hxRuleOptions & HX_OPT_HANDICAP) == 0 || s == PLAYER_NEUTRAL)
      continue;
    if (bOld) {
      g_abSlot[s] = g_bRecorded ? g_abRecorded[s] : HANDICAP_FULL;
    } else if (gPlayerData[s].nType == 1 || gPlayerData[s].nType == 2) {
      int n = HANDICAP_FULL;
      if (NetChosen()) {
        n = NetPercent(s);
      } else if (Session().bHandicaps) {
        const int r = RowOf(s);
        if (r >= 0)
          n = Session().aRow[r].bHandicap;
      }
      g_abSlot[s] = static_cast<BYTE>(HandicapClamp(n));
    }
  }
  g_bNet = false;
  g_hxHandicapOn = false;
  for (int s = 0; s < PLAYER_SLOTS_MAX; s++)
    if (g_abSlot[s] < HANDICAP_FULL) {
      g_hxHandicapOn = true;
      Log("slot %d (%s): %d%%", s, gPlayerData[s].szName, g_abSlot[s]);
    }
  if ((g_hxRuleOptions & HX_OPT_HANDICAP) != 0)
    Log("a game %s, %s", bOld ? "played back or loaded" : "new", g_hxHandicapOn ? "with handicaps" : "every slot at 100%");
}

int HandicapOf(int nPlayer) {
  return Percent(nPlayer);
}

LONG HandicapLifeOf(LONG lLife, int nPlayer) {
  const int n = Percent(nPlayer);
  if (n >= HANDICAP_FULL || lLife <= 0x100)
    return lLife;
  // whole points, rounded, at least one
  const LONG l = static_cast<LONG>((static_cast<long long>(lLife) * n / 100 + 0x80) & ~0xFFLL);
  return l < 0x100 ? 0x100 : l;
}

int HandicapShieldsOf(int nShields, int nPlayer) {
  const int n = Percent(nPlayer);
  if (n >= HANDICAP_FULL || nShields <= 1)
    return nShields;
  const int m = (nShields * n + 50) / 100;
  return m < 1 ? 1 : m;
}

void HandicapOwnerChanged(CUnit *pUnit, int nFrom) {
  if (!g_hxHandicapOn || Percent(nFrom) == Percent(pUnit->bPlayer))
    return;
  const LONG lFrom = HandicapLifeOf(gxUnitLife[pUnit->wType], nFrom);
  const LONG lTo = HandicapLifeOf(gxUnitLife[pUnit->wType], pUnit->bPlayer);
  if (lFrom > 0) {
    const long long l = static_cast<long long>(pUnit->lHitPoints) * lTo / lFrom;
    pUnit->lHitPoints = static_cast<LONG>(l < 1 ? 1 : l > lTo ? lTo : l);
  }
  const int nFromShields = HandicapShieldsOf(guwUnitEnergy[pUnit->wType], nFrom) << 8;
  const int nToShields = HandicapShieldsOf(guwUnitEnergy[pUnit->wType], pUnit->bPlayer) << 8;
  if (nFromShields > 0) {
    const long long l = static_cast<long long>(pUnit->lShields) * nToShields / nFromShields;
    pUnit->lShields = static_cast<DWORD>(l > nToShields ? nToShields : l);
  }
}

void HandicapsSave(BYTE *pbOut) {
  for (int s = 0; s < PLAYER_SLOTS_MAX; s++)
    pbOut[s] = static_cast<BYTE>(Percent(s));
}

void HandicapsRecorded(const BYTE *pbIn, int nSlots) {
  g_bRecorded = pbIn != NULL;
  for (int s = 0; s < PLAYER_SLOTS_MAX; s++)
    g_abRecorded[s] = static_cast<BYTE>(pbIn != NULL && s < nSlots ? HandicapClamp(pbIn[s]) : HANDICAP_FULL);
}
