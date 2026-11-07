// mutators_feature_test.cpp -- record compatibility, serialization and resource-fog checks.
// Build as C++17 with -ffunction-sections -fdata-sections and link with --gc-sections.
#include <assert.h>
#include <string.h>
#include <strings.h>
#include <string>
#include <vector>
#include <iostream>

#include "../../src117/port/gameplay/port_mutators.cpp"

bool g_hxPlayers = false;
int g_hxSlots = PLAYER_SLOTS_117;
DWORD *gpSquareMap = nullptr;
WORD gwMapWidth = 0, gwMapHeight = 0;
WORD guwUnitMineralsCost[228] = {};
WORD guwUnitGasesCost[228] = {};
WORD guwUnitTimeToBuild[228] = {};

PortSession::PortSession() { memset(abExtraRace, 0xFF, sizeof abExtraRace); }
static PortSession g_testSession;
PortSession &Session() { return g_testSession; }
const char *PortText(const char *s) { return s; }
const char *PortTextC(const char *, const char *s) { return s; }
const char *PortTextN(const char *one, const char *other, long long n) { return n == 1 ? one : other; }
void PortLogger::Write(const char *, ...) const {}
void PortLogger::Line(const char *) const {}
int __stdcall lstrcmpiA(LPCSTR a, LPCSTR b) { return strcasecmp(a, b); }

static std::string Fill(const char *text, std::initializer_list<std::string> args) {
  std::string out = text;
  size_t i = 0;
  for (const std::string &arg : args) {
    const std::string key = "{" + std::to_string(i++) + "}";
    size_t at = 0;
    while ((at = out.find(key, at)) != std::string::npos) {
      out.replace(at, key.size(), arg);
      at += arg.size();
    }
  }
  return out;
}
std::string PortTextFill(const char *s, std::initializer_list<std::string> args) { return Fill(s, args); }
std::string PortTextF(const char *s, std::initializer_list<std::string> args) { return Fill(s, args); }
std::string PortTextNF(const char *one, const char *other, long long n,
                       std::initializer_list<std::string> args) {
  return Fill(n == 1 ? one : other, args);
}

static void TestRecordAndSerialization() {
  const int oldSizes[] = {28, 40, 44, 56, 60, 64, 72, 100};
  unsigned char current[MUTATOR_RECORD_BYTES_VICTORY];
  g_game = MutatorSet();
  g_nRevision = MUTATOR_REVISION;
  MutatorsSave(current);
  for (int n : oldSizes) {
    assert(MutatorRecordBytesRead(n));
    assert(MutatorsRecorded(current, n));
    assert(g_recorded.nVictory == MUTATOR_VICTORY_CLASSIC);
    assert(g_recorded.nFog == MUTATOR_FOG_CLASSIC);
  }
  assert(!MutatorRecordBytesRead(101));
  assert(!MutatorRecordBytesRead(108));

  g_game.nFog = MUTATOR_FOG_RESOURCES;
  g_game.nVictory = MUTATOR_VICTORY_PRODUCTION;
  g_nRevision = MUTATOR_REVISION;
  MutatorsSave(current);
  DWORD tail = 0;
  memcpy(&tail, current + MUTATOR_RECORD_BYTES_NEWBIE, sizeof tail);
  assert(tail == MUTATOR_VICTORY_PRODUCTION);
  assert(MutatorsRecorded(current, sizeof current));
  assert(g_recorded.nVictory == MUTATOR_VICTORY_PRODUCTION);
  assert(g_recorded.nFog == MUTATOR_FOG_RESOURCES);
  assert(g_nRecordedRevision == MUTATOR_REVISION);
  assert(MutatorsJson(g_recorded).find("\"victory\":1") != std::string::npos);
  assert(MutatorsList(g_recorded) == "fog=resources,victory=production");
  assert(MutatorOn(g_recorded, MUT_VICTORY));
  assert(MutatorChoiceExact(g_recorded, MUT_VICTORY) == MUTATOR_VICTORY_PRODUCTION);
  MutatorSet chosen = g_recorded;
  MutatorChoose(&chosen, MUT_VICTORY, MUTATOR_VICTORY_CLASSIC);
  assert(chosen.nVictory == MUTATOR_VICTORY_CLASSIC);

  g_game = g_recorded;
  g_hxMutatorsOn = true;
  assert(MutatorProductionVictory());
  g_game.nVictory = MUTATOR_VICTORY_CLASSIC;
  assert(!MutatorProductionVictory());
}

static void TestCliChoiceAndText() {
  assert(MutatorsChooseList("fog=resources,victory=production"));
  assert(Session().bMutators);
  assert(Session().mut.nFog == MUTATOR_FOG_RESOURCES);
  assert(Session().mut.nVictory == MUTATOR_VICTORY_PRODUCTION);
  assert(MutatorsText(Session().mut) == "resources and obstacles remembered, production victory");
  assert(MutatorsList(Session().mut) == "fog=resources,victory=production");
  assert(MutatorsChooseList("victory=classic"));
  assert(Session().mut.nVictory == MUTATOR_VICTORY_CLASSIC);
}

static void TestResourceExplorationKeepsLiveHiddenBits() {
  gwMapWidth = gwMapHeight = 5;
  DWORD tiles[25];
  for (DWORD &tile : tiles)
    tile = 0xFFFFFFFFu;
  gpSquareMap = tiles;
  ExploreTilesFor(nullptr, 2 * 32, 2 * 32);
  for (int y = 0; y < 5; ++y) {
    for (int x = 0; x < 5; ++x) {
      const DWORD tile = tiles[y * 5 + x];
      const bool explored = x >= 1 && x <= 3 && y >= 1 && y <= 3;
      assert((tile & 0xFFu) == 0xFFu);  // no player's live hidden bits changed
      assert((tile & 0xFF00u) == (explored ? 0u : 0xFF00u));
      assert((tile >> 16) == 0xFFFFu);
    }
  }
}

static void TestScoutBalance() {
  guwUnitMineralsCost[0x46] = 275;
  guwUnitGasesCost[0x46] = 125;
  guwUnitTimeToBuild[0x46] = 1200;
  ScoutBalance();
  assert(guwUnitMineralsCost[0x46] == 240);
  assert(guwUnitGasesCost[0x46] == 115);
  assert(guwUnitTimeToBuild[0x46] == 1080);

  // UNIS values also receive deltas, never a forced stock row or an underflow.
  guwUnitMineralsCost[0x46] = 300;
  guwUnitGasesCost[0x46] = 130;
  guwUnitTimeToBuild[0x46] = 1000;
  ScoutBalance();
  assert(guwUnitMineralsCost[0x46] == 265);
  assert(guwUnitGasesCost[0x46] == 120);
  assert(guwUnitTimeToBuild[0x46] == 880);
  guwUnitMineralsCost[0x46] = 20;
  guwUnitGasesCost[0x46] = 5;
  guwUnitTimeToBuild[0x46] = 100;
  ScoutBalance();
  assert(guwUnitMineralsCost[0x46] == 0);
  assert(guwUnitGasesCost[0x46] == 0);
  assert(guwUnitTimeToBuild[0x46] == 1);
}

static void TestInitialResourceCopyHasOneOwner() {
  g_bFogResourcesInitial = true;
  assert(MutatorFogSuppressInitialCopy(0xB0));
  assert(MutatorFogSuppressInitialCopy(0xB1));
  assert(MutatorFogSuppressInitialCopy(0xB2));
  assert(MutatorFogSuppressInitialCopy(0xBC));
  assert(!MutatorFogSuppressInitialCopy(0x24));
  g_bFogResourcesInitial = false;
  assert(!MutatorFogSuppressInitialCopy(0xB0));
  assert(!MutatorFogSuppressInitialCopy(0xBC));
}

int main() {
  TestRecordAndSerialization();
  TestCliChoiceAndText();
  TestResourceExplorationKeepsLiveHiddenBits();
  TestScoutBalance();
  TestInitialResourceCopyHasOneOwner();
  std::cout << "mutator record, CLI, serialization and fog exploration checks passed\n";
}
