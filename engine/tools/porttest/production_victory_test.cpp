// production_victory_test.cpp -- production-victory side aggregation and unit filters.
// Build as C++17 with -ffunction-sections -fdata-sections and link with --gc-sections.
#include <assert.h>
#include <string.h>
#include <iostream>

#include "../../src117/lang/victory.cpp"

bool g_hxPlayers = false;
int g_hxSlots = PLAYER_SLOTS_117;
PlayerVictoryStateTable gbVictoryState;
PlayerTable<BYTE, PlayerTagParticipants, 8> sgpParticipants;
PlayerTable<BYTE, PlayerTagVictoryCodes, 8> sgbVictoryCodes;
PlayerAlliancesMatrix sgbAlliances_g;
PlayerTable<PlayerData, PlayerTagData> gPlayerData;
BYTE gubUnitCountInfo[228] OC_UNALIGNED;
DWORD gulUnitTypeFlags[228] OC_UNALIGNED;

static CUnit *g_testUnits = nullptr;
static BOOL g_testTeamGame = FALSE;

bool PortSnapStatic(void *, size_t) { return true; }
CUnit *__fastcall CUnit::GetUsedList() { return g_testUnits; }
BOOL CUnit::IsDeadOrDying() { return pSprite == nullptr || bOrderId == 0 || bOrderState == 1; }
BOOL IsTeamGame() { return g_testTeamGame; }

static void Reset() {
  g_testUnits = nullptr;
  g_testTeamGame = FALSE;
  for (int i = 0; i < 8; ++i) {
    sgpParticipants[i] = 1;
    gbVictoryState[i] = 0;
    sgbVictoryCodes[i] = 0;
    gPlayerData[i].nType = 2;
    gPlayerData[i].nTeam = 0;
    for (int j = 0; j < 12; ++j)
      sgbAlliances_g[i][j] = 0;
  }
  memset(gubUnitCountInfo, 0, sizeof gubUnitCountInfo);
  memset(gulUnitTypeFlags, 0, sizeof gulUnitTypeFlags);
}

static void Unit(CUnit *u, int player, int type, bool factory, bool subunit = false) {
  memset(u, 0, sizeof *u);
  u->bPlayer = static_cast<BYTE>(player);
  u->wType = static_cast<WORD>(type);
  u->pSprite = reinterpret_cast<CSprite *>(1);
  u->bOrderId = 1;  // incomplete is fine: completion is deliberately not tested
  gubUnitCountInfo[type] = factory ? 0x20 : 0;
  gulUnitTypeFlags[type] = subunit ? 0x10 : 0;
  u->pNext = g_testUnits;
  g_testUnits = u;
}

static void TestFreeForAllAndFilters() {
  Reset();
  CUnit units[6];
  Unit(&units[0], 0, 10, true);             // live, incomplete production
  Unit(&units[1], 1, 11, false);            // no production for player 1
  Unit(&units[2], 2, 12, true);
  Unit(&units[3], 2, 13, true, true);        // a subunit does not count
  Unit(&units[4], 1, 14, true);
  units[4].dwFlags |= 0x40000000u;           // a hallucination does not count
  Unit(&units[5], 2, 15, true);
  units[5].bOrderState = 1;                  // dying does not count; unit 12 keeps player 2 alive

  ProductionVictoryCheck();
  assert(sgbVictoryCodes[0] == 0);
  assert(sgbVictoryCodes[1] == 2);
  assert(sgbVictoryCodes[2] == 0);           // two production sides remain

  units[2].bOrderState = 1;
  ProductionVictoryCheck();
  assert(sgbVictoryCodes[0] == 3);           // only player 0 remains
  assert(sgbVictoryCodes[1] == 2);
  assert(sgbVictoryCodes[2] == 2);
}

static void TestTeamAggregate() {
  Reset();
  g_testTeamGame = TRUE;
  gPlayerData[0].nTeam = gPlayerData[1].nTeam = 1;
  gPlayerData[2].nTeam = 2;
  CUnit units[3];
  Unit(&units[0], 0, 20, true);              // player 1 has none, but shares player 0's
  Unit(&units[1], 1, 21, false);
  Unit(&units[2], 2, 22, true);

  ProductionVictoryCheck();
  assert(sgbVictoryCodes[0] == 0);
  assert(sgbVictoryCodes[1] == 0);           // one factory-less teammate cannot eliminate the team
  assert(sgbVictoryCodes[2] == 0);

  units[2].dwFlags |= 0x40000000u;
  ProductionVictoryCheck();
  assert(sgbVictoryCodes[0] == 3);
  assert(sgbVictoryCodes[1] == 3);
  assert(sgbVictoryCodes[2] == 2);
}

static void TestMutualAlliedVictoryAggregate() {
  Reset();
  sgbAlliances_g[0][1] = sgbAlliances_g[1][0] = 2;
  CUnit units[3];
  Unit(&units[0], 0, 30, true);
  Unit(&units[1], 1, 31, false);
  Unit(&units[2], 2, 32, true);
  ProductionVictoryCheck();
  assert(sgbVictoryCodes[0] == 0);
  assert(sgbVictoryCodes[1] == 0);
  assert(sgbVictoryCodes[2] == 0);
}

int main() {
  TestFreeForAllAndFilters();
  TestTeamAggregate();
  TestMutualAlliedVictoryAggregate();
  std::cout << "production victory side, team and unit-filter checks passed\n";
}
