// port_aitables.cpp -- how full the computer player's shared tables are, for Debug Tools.
//
// 1.17's AI keeps its records for every computer together in pools: 1,000 soldiers
// (gSoldiers_g), 1,000 guards (gGuards_g), 1,000 workers (gPeons_g), 1,000 buildings
// (gBldgs_g), 100 towns (gTowns_g) and 100 script threads (gScripts_g), and a game past 1.17's
// limits as many as its units (h/aipools.h). A unit made while its pool is empty gets no
// record (CUnit::dwUnion134 stays 0) and the AI never moves it again. The live channel's
// ai_tables reads them, and the units of each computer that hold no record. It only reads.
#include <windows.h>
#include <string>

#include "port_aitables.h"
#include "../platform/port_json.h"   // PortJson
#include "../tools/port_control.h"   // PortControlCommand
#include "../../h/CUnit.h"
#include "../../h/net_cmds.h"        // PlayerData
#include "../../h/playerlimits.h"    // PlayerLobbySlots, PlayerIsLobby
#include "../../h/playertables.h"
#include "../../h/TAI_Captain.h"     // TSoldierPool
#include "../../h/TAI_GuardPost.h"   // TGuardPool
#include "../../h/SAI_Build.h"       // TWorkerPool, TBldgPool, TTownPool
#include "../../h/SAI_Scripts.h"     // TScriptPool
#include "../../gen/globals117.h"    // gwGameMode, gPlayerData

extern TSoldierPool gSoldiers_g;
extern TGuardPool gGuards_g;
extern TWorkerPool gPeons_g;
extern TBldgPool gBldgs_g;
extern TTownPool gTowns_g;
extern TScriptPool gScripts_g;

namespace {

// The entries on a pool's free list, walked from its head, no further than the pool holds.
template <class T>
int FreeCount(T *pHead, int nMax)
{
  int n = 0;
  for (T *p = pHead; p != NULL && n <= nMax; p = p->pNext)
    ++n;
  return n;
}

std::string Pool(const char *pszName, int nSize, int nFree)
{
  return std::string("{\"name\":\"") + pszName + "\",\"size\":" + std::to_string(nSize) + ",\"used\":" +
         std::to_string(nSize - nFree) + ",\"free\":" + std::to_string(nFree) + "}";
}

bool Worker(int nType)
{
  return nType == 7 || nType == 41 || nType == 64;
}

bool Building(int nType)
{
  return nType >= 106 && nType <= 202;
}

// Larvae, eggs, cocoons, overlords, interceptors and scarabs: the AI keeps no soldier record
// for them.
bool NoSoldier(int nType)
{
  return nType == 35 || nType == 36 || nType == 59 || nType == 97 || nType == 42 || nType == 72 || nType == 73 ||
         nType == 85;
}

bool CmdAiTables(const PortJson &, std::string *pAnswer)
{
  if (gwGameMode != 3) {
    *pAnswer = "ai_tables needs a game";
    return false;
  }
  const int nRecords = g_aiPools.records, nTowns = g_aiPools.towns;
  std::string s = "\"pools\":[" + Pool("soldiers", nRecords, FreeCount(gSoldiers_g.pFreeHead, nRecords)) + "," +
                  Pool("guards", nRecords, FreeCount(gGuards_g.pFree, nRecords)) + "," +
                  Pool("workers", nRecords, FreeCount(gPeons_g.pFreeHead, nRecords)) + "," +
                  Pool("buildings", nRecords, FreeCount(gBldgs_g.pFreeHead, nRecords)) + "," +
                  Pool("towns", nTowns, FreeCount(gTowns_g.pFreeHead, nTowns)) + "," +
                  Pool("scripts", nTowns, FreeCount(gScripts_g.pFree, nTowns)) + "]";
  // each computer's finished units, and those of them with no record, by kind
  const int nSlots = PlayerLobbySlots();
  std::string players;
  int anAll[3] = {}, anLoose[3] = {};
  for (int p = 0; p < nSlots; ++p) {
    if (!PlayerIsLobby(static_cast<unsigned>(p)) || gPlayerData[p].nType != 1)
      continue;
    int an[3] = {}, anNone[3] = {};
    for (CUnit *pUnit = CUnit::GetUsedList(); pUnit != NULL; pUnit = pUnit->pNext) {
      if (pUnit->bPlayer != p || (pUnit->dwFlags & 1) == 0 || NoSoldier(pUnit->wType))
        continue;
      const int k = Worker(pUnit->wType) ? 1 : Building(pUnit->wType) ? 2 : 0;
      ++an[k];
      if (pUnit->dwUnion134 == 0)
        ++anNone[k];
    }
    for (int k = 0; k < 3; ++k) {
      anAll[k] += an[k];
      anLoose[k] += anNone[k];
    }
    players += std::string(players.empty() ? "" : ",") + "{\"slot\":" + std::to_string(p) +
               ",\"army\":" + std::to_string(an[0]) + ",\"armyNoRecord\":" + std::to_string(anNone[0]) +
               ",\"workers\":" + std::to_string(an[1]) + ",\"workersNoRecord\":" + std::to_string(anNone[1]) +
               ",\"buildings\":" + std::to_string(an[2]) + ",\"buildingsNoRecord\":" + std::to_string(anNone[2]) + "}";
  }
  s += ",\"computers\":{\"army\":" + std::to_string(anAll[0]) + ",\"armyNoRecord\":" + std::to_string(anLoose[0]) +
       ",\"workers\":" + std::to_string(anAll[1]) + ",\"workersNoRecord\":" + std::to_string(anLoose[1]) +
       ",\"buildings\":" + std::to_string(anAll[2]) + ",\"buildingsNoRecord\":" + std::to_string(anLoose[2]) + "}";
  *pAnswer = s + ",\"players\":[" + players + "]";
  return true;
}

}  // namespace

void PortAiTablesStart(void)
{
  PortControlCommand("ai_tables", CmdAiTables);
}
