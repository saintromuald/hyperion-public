// port_score.cpp -- the score screen's game (port_score.h; port_scoregame.h). The
// game's hooks note when each player went out and keep who played and the four tabs' numbers
// as the game ends; ScoreRead hands them to the screen with what do_score_dlg worked out.
// Presentation only: nothing here writes a table the simulation reads.
//
// The numbers are 1.17's, as its four fill functions make them (gluScore.cpp): Overview
// sub_462B60, Units sub_462C10, Structures sub_462B20, Resources sub_462BB0; the comments over
// sub_462B20 and sub_462C10 have Units and Structures the wrong way round, and the tab switch
// is right. The rows of glGameCounts_g are count.cpp's: 2 units produced, 3 lost, 4 killed,
// 5 the units' score, 6 the kills'; 9 structures constructed, 10 lost, 11 razed, 12 their
// score, 13 the razings'.
#include <string.h>
#include <string>
#include <SDL3/SDL.h>
#include <vector>
#include "port_score.h"
#include "port_scoregame.h"
#include "port_ai.h"          // AiProfileOf
#include "../../h/playertables.h"
#include "../../h/port_synchash.h"  // SyncSlotRead
#include "../../h/hxrules.h"   // HxRules, g_hxRuleOptions, HX_OPT_TEAMS
#include "../../h/net_cmds.h"  // PlayerData
#include "../../h/gametype.h"  // GameHeader, IsScenarioGame, team_get_slot_captain
#include "../../h/Teams.h"     // IsTeamGame, GetTeamName
#include "../../h/forces.h"    // GetForceName
#include "../../h/map_load.h"  // GetCurrentMap
#include "../../h/net_time.h"  // sub_47A780
#include "../../h/replay.h"    // replay_dlg_elapsed_seconds
#include "../render/port_teamcolor.h"  // PortTeamColorGet
#include "../render/port_maptext.h"   // MapTextUtf8
#include "../../gen/globals117.h"

extern GameHeader gGameHeader;
// each nation's victory state (victory.cpp), 3 a victory, 2 a defeat, 5 a draw, 1 dropped
extern PlayerVictoryStateTable gbVictoryState;

namespace {

// The second each slot's victory state left 0, -1 while it has not; the local player's
// leaving.
int g_anOutAt[PLAYER_SLOTS_MAX];
bool g_bLeft;
// The game as it ended (PortScoreGameEnd).
ScoreGame g_game;

int Now(void)
{
  return gPlayback != 0 ? replay_dlg_elapsed_seconds() : sub_47A780();
}

// A name from the game in UTF-8, StarCraft's colour codes (bytes under 32) left out: UTF-8 as
// typed in another language, else Windows-1252 as 1.17 keeps it (MapTextUtf8).
std::string Utf8(const char *psz, size_t nMax)
{
  return MapTextUtf8(std::string(psz, SDL_strnlen(psz, nMax)).c_str());
}

// The colour the game drew a slot in: its full RGB when it has one, else its minimap dot's
// entry of the game's palette, which says the player's colour as the minimap does.
unsigned ColourOf(int nSlot)
{
  unsigned char rgb[3];
  if (PortTeamColorGet(nSlot, rgb))
    return static_cast<unsigned>(rgb[0]) << 16 | rgb[1] << 8 | rgb[2];
  const BYTE *p = sgGamePalette_g + 4 * gbMinimapColor_g[nSlot];
  return static_cast<unsigned>(p[0]) << 16 | p[1] << 8 | p[2];
}

// A slot's numbers on the four tabs, read from nFrom: the slot itself, or in 1.17's team
// games its team's captain, whose nation the team plays.
void Values(int nFrom, int (*pan)[SCORE_CELLS])
{
  SyncSlot s;
  SyncSlotRead(nFrom, &s);
  const int *c = reinterpret_cast<const int *>(s.adwScores);
  const int nGas = s.nGasGathered, nMinerals = s.nMineralsGathered;
  int *o = pan[SCORE_OVERVIEW];
  o[0] = c[5] + c[6];
  o[1] = c[12] + c[13];
  o[2] = nGas + nMinerals;
  o[3] = o[0] + o[1] + o[2];
  int *u = pan[SCORE_UNITS];
  u[0] = c[2];
  u[1] = c[4];
  u[2] = c[3];
  u[3] = c[5] + c[6];
  int *b = pan[SCORE_STRUCTURES];
  b[0] = c[9];
  b[1] = c[11];
  b[2] = c[10];
  b[3] = c[12] + c[13];
  int *r = pan[SCORE_RESOURCES];
  r[0] = nGas;
  r[1] = nMinerals;
  const int nSpent = nGas + nMinerals - s.nMinerals - s.nGas;
  r[2] = nSpent < 0 ? 0 : nSpent;
  r[3] = nGas + nMinerals;
}

// The players 1.17's score screen lists (sub_461E00): computers, people, and those who left.
bool Listed(int nType)
{
  return nType == 1 || nType == 2 || nType == 10 || nType == 11;
}

// A victory state as a result (sub_477270's reading of it).
int ResultOf(BYTE bState)
{
  switch (bState) {
  case 3: return SCORE_WON;
  case 2: return SCORE_LOST;
  case 5: return SCORE_DRAW;
  case 1: return SCORE_DROPPED;
  default: return SCORE_PLAYING;
  }
}

}  // namespace

unsigned ScoreColour(int nSlot)
{
  return ColourOf(nSlot);
}

void PortScoreGameStart(void)
{
  for (int &n : g_anOutAt)
    n = -1;
  g_bLeft = false;
  g_game = ScoreGame();
}

void PortScoreFrame(void)
{
  const int n = PlayerLobbySlots();
  for (int i = 0; i < n; i++)
    if (g_anOutAt[i] < 0 && gbVictoryState[i] != 0)
      g_anOutAt[i] = Now();
}

void PortScoreLeft(void)
{
  g_bLeft = true;
}

void PortScoreGameEnd(void)
{
  ScoreGame g;
  g.bValid = true;
  g.bExpansion = gbIsExpansion != 0;
  g.bHyperion = HxRules();
  g.nGameType = gGameHeader.bGameType;
  // the time of leaving read as the game ends, not in Quit Mission's menu: 1.17's clock takes a
  // pause back out only when it ends, so the menu's own pause would count until then
  g.bLeft = g_bLeft;
  g.nLeftAt = g_bLeft ? Now() : -1;
  if (const char *pszMap = static_cast<const char *>(GetCurrentMap(1))) {
    const char *pszName = strrchr(pszMap, '\\');
    std::string name = pszName != NULL ? pszName + 1 : pszMap;
    const size_t nDot = name.rfind('.');
    g.map = Utf8(name.substr(0, nDot).c_str(), name.size());
  }
  // teams: Hyperion's chosen ones, 1.17's team games, or a scenario's forces
  const bool bHxTeams = (g_hxRuleOptions & HX_OPT_TEAMS) != 0;
  const bool bTeamGame = !bHxTeams && IsTeamGame();
  const bool bForces = !bHxTeams && !bTeamGame && IsScenarioGame(NULL);
  g.bShared = bTeamGame;
  std::vector<int> teamOf;  // a team's number in the game, by its place in g.teams
  const int n = PlayerLobbySlots();
  for (int i = 0; i < n; i++) {
    if (i == PLAYER_NEUTRAL || !Listed(gPlayerData[i].nType))
      continue;
    ScorePlayer p;
    p.nSlot = i;
    p.name = Utf8(gPlayerData[i].szName, sizeof gPlayerData[i].szName);
    p.nRace = gPlayerData[i].nRace <= 2 ? gPlayerData[i].nRace : 1;
    p.rgb = ColourOf(i);
    p.bComputer = gPlayerData[i].nType == 1 || gPlayerData[i].nType == 11;
    p.nLevel = p.bComputer ? AiProfileOf(i).bLevel : 0;
    p.bYou = gPlayback == 0 && i == static_cast<int>(g_LocalNationID);
    p.nOutAt = g_anOutAt[i];
    const int nTeam = gPlayerData[i].nTeam;
    if ((bHxTeams && nTeam != 0) || bTeamGame || bForces) {
      int q = 0;
      while (q < static_cast<int>(teamOf.size()) && teamOf[q] != nTeam)
        ++q;
      if (q == static_cast<int>(teamOf.size())) {
        teamOf.push_back(nTeam);
        ScoreTeam t;
        t.nIndex = bHxTeams ? nTeam - 1 : nTeam;
        if (bHxTeams) {
          t.name = "Team " + std::to_string(nTeam);
        } else if (bTeamGame) {
          const char *psz = GetTeamName(static_cast<BYTE>(nTeam));
          t.name = psz != NULL ? Utf8(psz, 64) : "Team " + std::to_string(nTeam + 1);
        } else {
          char sz[0x1E] = "";
          GetForceName(nTeam, sz, sizeof sz);
          t.name = sz[0] != 0 ? Utf8(sz, sizeof sz) : "Force " + std::to_string(nTeam + 1);
        }
        g.teams.push_back(t);
      }
      p.nTeam = q;
    }
    Values(bTeamGame ? team_get_slot_captain(static_cast<BYTE>(nTeam)) : i, p.anValue);
    g.players.push_back(p);
  }
  // a scenario whose forces each hold one player has no teams to show
  if (bForces && g.teams.size() == g.players.size()) {
    g.teams.clear();
    for (ScorePlayer &p : g.players)
      p.nTeam = -1;
  }
  g_game = g;
}

ScoreGame ScoreRead(const ScoreEnd &end)
{
  ScoreGame g = g_game;
  g.end = end;
  for (ScorePlayer &p : g.players) {
    p.nResult = ResultOf(gbVictoryState[p.nSlot]);
    // the local player's as 1.17 has it: left or still playing is a defeat
    if (p.bYou)
      p.nResult = end.nLocal == 1 ? SCORE_WON : end.nLocal == 3 ? SCORE_DRAW : end.nLocal == 4 ? SCORE_DROPPED
                                                                                               : SCORE_LOST;
  }
  return g;
}

ScoreGame ScoreStandIn(const char *pszPreset)
{
  struct Preset {
    const char *pszName;
    int nPlayers, nTeams;
  };
  static const Preset kPresets[] = {{"1v1", 2, 0}, {"ffa8", 8, 0}, {"4v4", 8, 2}, {"ffa16", 16, 0}, {"4x8", 32, 4},
                                    {"ffa32", 32, 0}};
  Preset preset = kPresets[1];
  for (const Preset &p : kPresets)
    if (strcmp(p.pszName, pszPreset) == 0)
      preset = p;
  static const char *const kNames[] = {"Raynor", "Kerrigan", "Tassadar", "Zeratul", "Fenix", "Duran", "Mengsk",
                                       "Artanis", "Aldaris", "Stukov", "DuGalle", "Raszagal"};
  static const unsigned kColours[] = {0xf40404, 0x0c48cc, 0x2cb494, 0x88409c, 0xf88c14, 0x703014, 0xcce0d0, 0xfcfc38};
  unsigned seed = 0x5C0E;
  const auto Next = [&](int n) {
    seed = seed * 22695477u + 1u;  // 1.17's own generator
    return static_cast<int>((seed >> 16 & 0x7FFF) % static_cast<unsigned>(n));
  };
  ScoreGame g;
  g.bValid = true;
  g.map = "Stand-in";
  g.nGameType = preset.nTeams > 0 ? 0x0B : 0x03;
  g.bExpansion = true;
  g.bHyperion = preset.nPlayers > 8;
  g.end = {1, false, false, 1462};
  for (int q = 0; q < preset.nTeams; ++q) {
    ScoreTeam t;
    t.nIndex = q;
    t.name = "Team " + std::to_string(q + 1);
    g.teams.push_back(t);
  }
  for (int i = 0; i < preset.nPlayers; ++i) {
    ScorePlayer p;
    p.nSlot = i;
    p.name = std::string(kNames[i % 12]) + (i >= 12 ? " " + std::to_string(i / 12 + 1) : "");
    p.nRace = Next(3);
    p.rgb = kColours[i % 8];
    p.bComputer = i != 0;
    p.nLevel = p.bComputer ? 1 + Next(AI_LEVEL_ELITE) : 0;
    p.nTeam = preset.nTeams > 0 ? i % preset.nTeams : -1;
    p.bYou = i == 0;
    const bool bWon = preset.nTeams > 0 ? p.nTeam == 0 : i == 0;
    p.nResult = bWon ? SCORE_WON : SCORE_LOST;
    p.nOutAt = bWon ? -1 : 300 + Next(1100);
    const int nStrength = bWon ? 140 : 40 + Next(90);
    const int nGas = nStrength * (20 + Next(30)), nMinerals = nStrength * (60 + Next(60));
    const int nUnits = nStrength / 2 + Next(40), nBuilt = nStrength / 6 + Next(12);
    int c[14] = {};
    c[2] = nUnits;
    c[4] = Next(nStrength);
    c[3] = nUnits - Next(nUnits / 2 + 1);
    c[5] = nUnits * 25 * (2 + Next(4));
    c[6] = c[4] * 50 * (1 + Next(3));
    c[9] = nBuilt;
    c[11] = Next(nBuilt + 1);
    c[10] = bWon ? Next(3) : nBuilt;
    c[12] = nBuilt * 100 * (2 + Next(3));
    c[13] = c[11] * 200;
    int (*v)[SCORE_CELLS] = p.anValue;
    v[SCORE_OVERVIEW][0] = c[5] + c[6];
    v[SCORE_OVERVIEW][1] = c[12] + c[13];
    v[SCORE_OVERVIEW][2] = nGas + nMinerals;
    v[SCORE_OVERVIEW][3] = v[SCORE_OVERVIEW][0] + v[SCORE_OVERVIEW][1] + v[SCORE_OVERVIEW][2];
    v[SCORE_UNITS][0] = c[2];
    v[SCORE_UNITS][1] = c[4];
    v[SCORE_UNITS][2] = c[3];
    v[SCORE_UNITS][3] = c[5] + c[6];
    v[SCORE_STRUCTURES][0] = c[9];
    v[SCORE_STRUCTURES][1] = c[11];
    v[SCORE_STRUCTURES][2] = c[10];
    v[SCORE_STRUCTURES][3] = c[12] + c[13];
    v[SCORE_RESOURCES][0] = nGas;
    v[SCORE_RESOURCES][1] = nMinerals;
    v[SCORE_RESOURCES][2] = (nGas + nMinerals) * (80 + Next(20)) / 100;
    v[SCORE_RESOURCES][3] = nGas + nMinerals;
    g.players.push_back(p);
  }
  return g;
}

bool ScoreBefore(int nResultA, long long nScoreA, int nSlotA, int nResultB, long long nScoreB, int nSlotB)
{
  const auto Band = [](int r) { return r == SCORE_WON ? 2 : r == SCORE_LOST ? 0 : 1; };
  if (Band(nResultA) != Band(nResultB))
    return Band(nResultA) > Band(nResultB);
  if (nScoreA != nScoreB)
    return nScoreA > nScoreB;
  return nSlotA < nSlotB;
}

int ScoreScaleOf(int nValue)
{
  if (nValue <= 0)
    return 1;
  const int s = 0x100 / nValue;
  if (s == 0)
    return 1;
  if (s <= 8)
    return s > 1 ? 8 : s;
  return 16;
}

long long ScoreStep(long long nAnim, long long nTarget)
{
  long long nStep = (nTarget - nAnim) / 8;
  if (nStep < 4)
    nStep = 4;
  return nAnim + nStep > nTarget ? nTarget : nAnim + nStep;
}
