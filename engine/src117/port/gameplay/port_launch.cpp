// port_launch.cpp -- every way into a game without the menus: --replay, --map (with --race,
// --computers and the rest), --load and --canvas (port_paths.cpp takes the options out), and
// the launches the front end, the benchmark, the trainer and the live channel call
// (docs/modules/gameplay.md). Each takes the menu steps it stands for. A replay: the browser
// reads the header and map (ReadReplayInfo, gluMap.cpp), its Ok seats the local player and
// calls CreateNewGame, and its close sets game mode 1 for RunGame (do_create_dlg); a melee
// map: Play Custom's, which keeps each player's race (InitSinglePlayerCustom) for
// SinglePlayerMeleeInitGame; a save, any save, as single player: the Load screen's Ok
// (load_game: sub_498CD0, then sub_443D20 as the game starts).
//
// PortLaunch runs once, from RunGlues, where 1.17's mission skip starts a mission. A replay
// plays in the mode it was recorded in, a map with Brood War when installed, against random
// computers, by default one in every other start, at the last game's speed; the local player
// takes the mission skip's name. What cannot be played leaves the main menu to come up. The
// replay suite starts its runs this way, as menus stop while the window is in the background.
#include <stdio.h>
#include <string.h>
#include "../tools/port_canvas.h"  // PortCanvasWrite
#include "port_launch.h"
#include "../platform/port_paths.h"  // PortOptions, PortCommandOptions
#include "oc_data.h"       // OC_VA, where 1.17's data is
#include "Storm.h"        // SStrCopy
#include "error.h"        // error_get_str
#include "grid.h"         // IsBroodwar
#include "game.h"         // PreloadMap
#include "gametype.h"     // IsMeleeTemplate
#include "gluBNCreate.h"  // gametype_find_template
#include "gluMap.h"       // CreateScenarioGame
#include "map_load.h"     // PreloadCampaignMap
#include "net_glue.h"     // CreateGame_SinglePlayer
#include "replay.h"       // LoadReplayData, replay_assign_gamedata, GetReplayMission, map_free
#include "uiSingle.h"     // seat_local_player
#include "saveload.h"     // sub_498CD0
#include "playertables.h"  // PlayersSetExtraComputers
#include "../data/port_moddata.h"  // ModRaceByName, ModRaceChoose
#include "port_teams.h"    // TeamsChooseList
#include "port_ai.h"       // AiChooseList
#include "port_handicap.h"  // HandicapChooseList, HandicapChoose
#include "port_mutators.h"  // MutatorsChooseList, MutatorsChoose
#include "port_session.h"  // SessionClear
#include "../front/port_frontscreens.h"  // SkirmishGame, the skirmish set-up's record
#include "../../h/hxrules.h"      // RulesChooseNext
#include <stdlib.h>         // strtol
#include "../../gen/globals117.h"
#include "../platform/port_globals117.h"

enum {
  GAME_HEADER = 0x8D, SLOTS = 12, SLOT = 0x24, MAP_PROBE = 0x20,
  // The melee template: category 2, the main template of its kind, which is
  // what Play Custom selects first.
  MELEE = 2, MELEE_KEY = MELEE | 1 << 16,
  // Free For All: every computer for itself, where a single-player melee puts them all on one
  // side; a benchmark's game type unless --bench-type melee.
  FFA = 3, FFA_KEY = FFA | 1 << 16,
  // Use Map Settings: the map's own players, forces and triggers.
  SCENARIO = 10, SCENARIO_KEY = SCENARIO | 1 << 16,
  RACE_RANDOM = 6, RACE_NONE = 7, COMPUTERS = 7,
  // SkirmishSeat's: a mod's race n is 0x100 + n
  RACE_MOD = 0x100,
};

// The races --race takes, as the Play Custom race list has them (0x503DA4).
static const struct {
  const char *pszName;
  BYTE bRace;
} kRaces[] = {{"zerg", 0}, {"terran", 1}, {"protoss", 2}, {"random", RACE_RANDOM}};

// The replay browser's steps for the file at pszFile (ReadReplayInfo, CreateNewGame).
static bool LaunchReplay(const char *pszFile)
{
  char szPath[MAX_PATH];
  BYTE abHeader[GAME_HEADER];
  BYTE abSlots[SLOTS][SLOT];
  BYTE abProbe[MAP_PROBE];
  int bExpansion;
  WORD wMission;

  SStrCopy(szPath, pszFile, MAX_PATH);
  gPlayback = 1;
  // An expansion replay loads only with the expansion on.
  gbIsExpansion = static_cast<BYTE>(IsBroodwar() != 0);
  if (!LoadReplayData(szPath, &bExpansion))
    return false;
  gbIsExpansion = static_cast<BYTE>(bExpansion != 0);
  memset(abProbe, 0, sizeof abProbe);
  wMission = GetReplayMission();
  if ((wMission != 0 ? PreloadCampaignMap(wMission, abProbe) : PreloadMap(szPath, abProbe, 0)) == 0)
    return false;
  // The Ok: Play Custom's melee defaults, a random local player and no computers, which a save
  // of the game carries for its load to seat; then the replay's header, as the local player
  byte_5264C3_g = 1;
  byte_526546_g = RACE_RANDOM;
  memset(byte_526547_g, RACE_NONE, 8);
  replay_assign_gamedata(abHeader, abSlots);
  SStrCopy(reinterpret_cast<char *>(abHeader + 4), gLocalPlayerName, 0x18);
  return seat_local_player() && CreateGame_SinglePlayer(reinterpret_cast<intptr_t>(abHeader));
}

// A race by name: one of kRaces, or a mod's, which plays as its base. False for
// a name that is neither.
static bool RaceByName(const char *pszName, BYTE *pbRace, int *pnModRace)
{
  int i = 0;
  while (i < ARRAYSIZE(kRaces) && lstrcmpiA(pszName, kRaces[i].pszName) != 0)
    ++i;
  *pnModRace = i == ARRAYSIZE(kRaces) ? ModRaceByName(pszName) : 0;
  if (i == ARRAYSIZE(kRaces) && *pnModRace == 0)
    return false;
  *pbRace = *pnModRace ? static_cast<BYTE>(ModRaceBase(*pnModRace)) : kRaces[i].bRace;
  return true;
}

// Play Custom's steps for a melee game on the map in pOptions (InitSinglePlayerCustom, then
// 1.17's own one-map starter, CreateScenarioGame).
static bool LaunchMap(const PortOptions *pOptions)
{
  char szPath[MAX_PATH];
  BYTE bRace = RACE_RANDOM;
  BYTE bComputerRace = RACE_RANDOM;
  int nComputerModRace = 0;
  int nComputers = COMPUTERS;

  // A mod's race plays as its base, and the game gives the slot the race: the
  // local player's, and with --computer-race the computers' Play Custom seats.
  if (pOptions->szRace[0] != '\0') {
    int nModRace = 0;
    if (!RaceByName(pOptions->szRace, &bRace, &nModRace))
      return false;
    ModRaceChoose(nModRace);
  }
  if (pOptions->szComputerRace[0] != '\0' &&
      !RaceByName(pOptions->szComputerRace, &bComputerRace, &nComputerModRace))
    return false;
  // A benchmark: every player a computer, the local one too, so the count is the
  // players less one; --bench-races gives a race a seat, the local player's first.
  const char *pszSeats = pOptions->bBench || pOptions->bTrainer ? pOptions->szBenchRaces : "";
  const size_t nSeats = strlen(pszSeats);
  auto seatRace = [&](size_t i, BYTE bOther) -> BYTE {
    const char c = i < nSeats ? static_cast<char>(tolower(pszSeats[i])) : '\0';
    return c == 'z' ? 0 : c == 't' ? 1 : c == 'p' ? 2 : c == 'r' ? RACE_RANDOM : bOther;
  };
  if (pOptions->bBench) {
    if (pOptions->szRace[0] == '\0') {
      bRace = seatRace(0, bComputerRace);
      ModRaceChoose(nSeats ? 0 : nComputerModRace);
    }
    const int nPlayers = pOptions->szPlayers[0] != '\0' ? atoi(pOptions->szPlayers) : 8;
    if (nPlayers < 2 || nPlayers > PLAYERS_MAX)
      return false;
    nComputers = nPlayers - 1;
    gOptions = 6;  // Fastest, which its replay keeps
  }
  // Up to 7 computers go in Play Custom's table; on a Hyperion map up to PLAYERS_MAX - 1,
  // the rest in port_players.cpp's.
  if (pOptions->szComputers[0] != '\0' && !pOptions->bBench) {
    const char *p = pOptions->szComputers;
    char *pEnd;
    nComputers = static_cast<int>(strtol(p, &pEnd, 10));
    if (nComputers < 0 || nComputers > PLAYERS_MAX - 1 || *pEnd != '\0')
      return false;
  }

  gPlayback = 0;
  gbIsExpansion = static_cast<BYTE>(IsBroodwar() != 0);
  // a trainer session's game is each for itself when asked
  const bool bFfa = pOptions->bBench ? _stricmp(pOptions->szBenchType, "melee") != 0
                                     : pOptions->bTrainer && _stricmp(pOptions->szBenchType, "ffa") == 0;
  if (!IsMeleeTemplate(bFfa ? FFA : MELEE, 0, 1))
    return false;
  byte_5264C3_g = 1;
  byte_526546_g = bRace;
  memset(byte_526547_g, RACE_NONE, 8);
  for (int i = 0; i < nComputers && i < COMPUTERS; ++i) {
    byte_526547_g[i] = seatRace(i + 1, bComputerRace);
    ModRaceChooseComputer(i, nSeats ? 0 : nComputerModRace);
  }
  // With no count asked for, a Hyperion map's slots past 7 take one each, as the menus
  // give them (port_players.cpp).
  PlayersSetExtraComputers(pOptions->szComputers[0] == '\0' && !pOptions->bBench ? -1
                           : nComputers > COMPUTERS ? nComputers - COMPUTERS : 0, bComputerRace);
  for (int i = COMPUTERS; i < nComputers && nSeats != 0; ++i)
    PlayersSetExtraComputerRace(i - COMPUTERS, seatRace(i + 1, bComputerRace));
  // the teams asked for, a player each in the set-up's order (port_teams.h)
  if (pOptions->szTeams[0] != '\0')
    TeamsChooseList(pOptions->szTeams);
  if (pOptions->szStarts[0] != '\0')
    TeamsChooseStartList(pOptions->szStarts);
  // the computers' levels, a player each in the same order (port_ai.h)
  if (pOptions->szAiLevels[0] != '\0')
    AiChooseList(pOptions->szAiLevels);
  // and their handicaps (port_handicap.h)
  if (pOptions->szHandicaps[0] != '\0')
    HandicapChooseList(pOptions->szHandicaps);
  // and the game's mutators (port_mutators.h)
  if (pOptions->szMutators[0] != '\0')
    MutatorsChooseList(pOptions->szMutators);
  SStrCopy(szPath, pOptions->szMap, MAX_PATH);
  return CreateScenarioGame(szPath, gLocalPlayerName, 0, bFfa ? FFA_KEY : MELEE_KEY, static_cast<BYTE>(gOptions)) == 0;
}

// Play Custom's steps for a Use Map Settings game on the map at pszMap: the map seats the
// players and its triggers run the game.
static bool LaunchScenario(const char *pszMap)
{
  char szPath[MAX_PATH];

  gPlayback = 0;
  gbIsExpansion = static_cast<BYTE>(IsBroodwar() != 0);
  if (gametype_find_template(SCENARIO, 0, 1) == NULL)
    return false;
  byte_5264C3_g = 1;
  byte_526546_g = RACE_RANDOM;
  memset(byte_526547_g, RACE_NONE, 8);
  SStrCopy(szPath, pszMap, MAX_PATH);
  return CreateScenarioGame(szPath, gLocalPlayerName, 0, SCENARIO_KEY, static_cast<BYTE>(gOptions)) == 0;
}

// Debug Tools' canvas: made, then played as a Use Map Settings game.
static bool g_bCanvasStarting;

static bool LaunchCanvas(const char *pszTileset)
{
  char szPath[MAX_PATH];
  g_bCanvasStarting = PortCanvasWrite(pszTileset, szPath, sizeof szPath) && LaunchScenario(szPath);
  return g_bCanvasStarting;
}

static bool g_bCanvasGame;
// a map the live channel loads (map_load): a game an editor watches, which the tips would pause
static bool g_bChannelStarting;

bool PortLaunchGameStarts(void)
{
  g_bCanvasGame = g_bCanvasStarting;
  g_bCanvasStarting = false;
  const bool bChannel = g_bChannelStarting;
  g_bChannelStarting = false;
  return g_bCanvasGame || bChannel;
}

bool PortLaunchIsCanvas(void)
{
  return g_bCanvasGame;
}

// The Load screen's Ok for the save at pszFile (load_game).
static bool LaunchLoad(const char *pszFile)
{
  gPlayback = 0;
  // The save's own flag must match (sub_499C50).
  gbIsExpansion = static_cast<BYTE>(IsBroodwar() != 0);
  return sub_498CD0(pszFile) != 0;
}

// The game mode a started game needs, or what the dialogs' Cancel leaves, and no choice left
// behind for a later game (port_session.h).
static int LaunchDone(bool bStarted, BYTE bWasExpansion)
{
  if (bStarted) {
    gwGameMode = 1;
    return TRUE;
  }
  gPlayback = 0;
  map_free();
  gbIsExpansion = bWasExpansion;
  SessionClear();
  return FALSE;
}

int PortLaunch(void)
{
  static bool s_bDone;
  const PortOptions *pOptions = PortCommandOptions();
  BYTE bWasExpansion = gbIsExpansion;
  bool bStarted;

  if (s_bDone || gbMultiPlayerMode != 0)
    return FALSE;
  s_bDone = true;
  if (pOptions->szReplay[0] == '\0' && pOptions->szMap[0] == '\0' && pOptions->szLoad[0] == '\0' &&
      pOptions->szCanvas[0] == '\0')
    return FALSE;
  if (gLocalPlayerName[0] == '\0')
    SStrCopy(gLocalPlayerName, error_get_str(0x48), 25);

  bStarted = pOptions->szReplay[0] != '\0' ? LaunchReplay(pOptions->szReplay)
             : pOptions->szLoad[0] != '\0'   ? LaunchLoad(pOptions->szLoad)
             : pOptions->szCanvas[0] != '\0' ? LaunchCanvas(pOptions->szCanvas)
             : pOptions->bUms               ? LaunchScenario(pOptions->szMap)
                                           : LaunchMap(pOptions);
  return LaunchDone(bStarted, bWasExpansion);
}

bool PortLaunchReplayFile(const char *pszPath)
{
  const BYTE bWasExpansion = gbIsExpansion;
  const WORD wMode = gwGameMode;
  const bool bStarted = LaunchDone(LaunchReplay(pszPath), bWasExpansion) != FALSE;
  gwGameMode = wMode;
  return bStarted;
}

// sub_498CD0 sets the game mode itself, in single player.
bool PortLaunchLoadFile(const char *pszPath)
{
  const BYTE bWasExpansion = gbIsExpansion;
  const WORD wMode = gwGameMode;
  gPlayback = 0;
  const bool bStarted = LaunchDone(sub_498CD0(pszPath) != 0, bWasExpansion) != FALSE;
  gwGameMode = wMode;
  return bStarted;
}

void PortLaunchStart(void)
{
  gwGameMode = 1;
}

int PortLaunchBench(const char *pszMap, const char *pszPlayers, const char *pszRace, const char *pszSeatRaces,
                    const char *pszTeams, const char *pszStarts, const char *pszAiLevels, const char *pszType,
                    const char *pszMutators)
{
  const BYTE bWasExpansion = gbIsExpansion;
  if (gbMultiPlayerMode != 0)
    return FALSE;
  if (gLocalPlayerName[0] == '\0')
    SStrCopy(gLocalPlayerName, error_get_str(0x48), 25);
  static PortOptions s_options;
  s_options = *PortCommandOptions();
  SStrCopy(s_options.szMap, pszMap, sizeof s_options.szMap);
  SStrCopy(s_options.szPlayers, pszPlayers, sizeof s_options.szPlayers);
  SStrCopy(s_options.szComputerRace, _stricmp(pszRace, "random") == 0 ? "" : pszRace,
           sizeof s_options.szComputerRace);
  SStrCopy(s_options.szBenchRaces, pszSeatRaces, sizeof s_options.szBenchRaces);
  SStrCopy(s_options.szBenchType, pszType, sizeof s_options.szBenchType);
  SStrCopy(s_options.szTeams, pszTeams, sizeof s_options.szTeams);
  SStrCopy(s_options.szStarts, pszStarts, sizeof s_options.szStarts);
  SStrCopy(s_options.szAiLevels, pszAiLevels, sizeof s_options.szAiLevels);
  SStrCopy(s_options.szMutators, pszMutators, sizeof s_options.szMutators);
  s_options.szRace[0] = '\0';
  s_options.szComputers[0] = '\0';
  s_options.bUms = false;
  s_options.bBench = true;
  return LaunchDone(LaunchMap(&s_options), bWasExpansion);
}

int PortLaunchTrainer(const char *pszMap, const char *pszRace, const char *pszSeatRaces, int nComputers, bool bFfa)
{
  const BYTE bWasExpansion = gbIsExpansion;
  if (gbMultiPlayerMode != 0)
    return FALSE;
  if (gLocalPlayerName[0] == '\0')
    SStrCopy(gLocalPlayerName, error_get_str(0x48), 25);
  static PortOptions s_options;
  s_options = *PortCommandOptions();
  SStrCopy(s_options.szMap, pszMap, sizeof s_options.szMap);
  SStrCopy(s_options.szRace, pszRace, sizeof s_options.szRace);
  snprintf(s_options.szComputers, sizeof s_options.szComputers, "%d", nComputers);
  s_options.szComputerRace[0] = '\0';
  SStrCopy(s_options.szBenchRaces, pszSeatRaces, sizeof s_options.szBenchRaces);
  SStrCopy(s_options.szBenchType, bFfa ? "ffa" : "melee", sizeof s_options.szBenchType);
  s_options.szTeams[0] = '\0';
  s_options.szStarts[0] = '\0';
  s_options.szAiLevels[0] = '\0';
  s_options.bUms = false;
  s_options.bBench = false;
  s_options.bTrainer = true;
  return LaunchDone(LaunchMap(&s_options), bWasExpansion);
}

unsigned char PortLaunchRaceBase(int nRace)
{
  return static_cast<BYTE>(nRace >= RACE_MOD ? ModRaceBase(nRace - RACE_MOD) : nRace);
}

void PortLaunchSkirmishChoices(const SkirmishGame &g)
{
  auto modRace = [](int nRace) { return nRace >= RACE_MOD ? nRace - RACE_MOD : 0; };
  const SkirmishSeat &me = g.seats[0];
  ModRaceChoose(modRace(me.nRace));
  if (!MutatorsStandard(g.mut))
    MutatorsChoose(g.mut);
  if (g.bColours)
    PlayersChooseColour(0, me.nColour);
  for (size_t r = 0; r < g.seats.size(); ++r) {
    const SkirmishSeat &s = g.seats[r];
    const int nRow = static_cast<int>(r);
    if (s.nTeam > 0)
      TeamsChoose(nRow, s.nTeam);
    // the player's own row has a level only when it is a computer's, in a watched game
    if ((r > 0 || g.bObserve) && s.nLevel >= 0)
      AiChoose(nRow, s.nLevel, AI_STYLE_OWN);
    if (s.nHandicap < 100)
      HandicapChoose(nRow, s.nHandicap);
    if (s.nStart >= 0)
      TeamsChooseStart(nRow, s.nStart);
    if (s.rgb != 0)
      PlayersChooseColourRgb(nRow, static_cast<unsigned char>(s.rgb >> 16), static_cast<unsigned char>(s.rgb >> 8),
                             static_cast<unsigned char>(s.rgb));
    // a computer's colour, past the seventh too: the computers past Play Custom's are seated in
    // turn after them (PlayersSeatNote), so row 1 + k is the k-th seated, as teams and levels
    // take it; a seat in full RGB, a team's shade or its own colour, has it from above
    else if (r > 0 && g.bColours)
      PlayersChooseColour(nRow, s.nColour);
  }
  // a melee game's computers: the first seven Play Custom's, the rest past them
  if (g.bMelee) {
    int nExtra = 0;
    BYTE abExtra[PLAYERS_MAX];
    for (size_t r = 1; r < g.seats.size(); ++r) {
      const int nRace = g.seats[r].nRace;
      if (r <= COMPUTERS) {
        ModRaceChooseComputer(static_cast<int>(r) - 1, modRace(nRace));
      } else if (nExtra < PLAYERS_MAX) {
        abExtra[nExtra++] = PortLaunchRaceBase(nRace);
      }
    }
    PlayersSetExtraComputers(nExtra, nExtra ? abExtra[0] : RACE_RANDOM);
    for (int e = 0; e < nExtra; ++e)
      PlayersSetExtraComputerRace(e, abExtra[e]);
  }
  RulesChooseNext(g.bHyperion ? 1 : 0);
}

// A game the skirmish set-up recorded (port_frontscreens.h): seated as its Start Game seats one
// (its choices, and the races Play Custom records), then made by CreateScenarioGame, which
// takes all of it before it makes the game.
int PortLaunchSkirmishGame(const SkirmishGame &g)
{
  const BYTE bWasExpansion = gbIsExpansion;
  if (gbMultiPlayerMode != 0 || g.seats.empty())
    return FALSE;
  if (gLocalPlayerName[0] == '\0')
    SStrCopy(gLocalPlayerName, error_get_str(0x48), 25);
  gPlayback = 0;
  gbIsExpansion = static_cast<BYTE>(IsBroodwar() != 0);
  PortLaunchSkirmishChoices(g);
  byte_5264C3_g = 1;
  byte_526546_g = PortLaunchRaceBase(g.seats[0].nRace);
  memset(byte_526547_g, RACE_NONE, 8);
  if (g.bMelee)
    for (size_t r = 1; r < g.seats.size() && r <= COMPUTERS; ++r)
      byte_526547_g[r - 1] = PortLaunchRaceBase(g.seats[r].nRace);
  char szPath[MAX_PATH];
  SStrCopy(szPath, g.map.c_str(), MAX_PATH);
  return LaunchDone(CreateScenarioGame(szPath, gLocalPlayerName, 0, g.nKey, static_cast<BYTE>(g.nSpeed)) == 0,
                    bWasExpansion);
}

int PortLaunchTrainerReplay(const char *pszPath)
{
  const BYTE bWasExpansion = gbIsExpansion;
  // the game's name, as the map's launch gives it: Storm makes no game of an empty one
  if (gLocalPlayerName[0] == '\0')
    SStrCopy(gLocalPlayerName, error_get_str(0x48), 25);
  return LaunchDone(LaunchReplay(pszPath), bWasExpansion);
}

// A replay watched: a LAN match's as it is so far (port_lanfeed.cpp), a game of the AI
// lab's farm (port_labwatch.cpp).
int PortLaunchWatchReplay(const char *pszPath)
{
  const BYTE bWasExpansion = gbIsExpansion;
  if (gLocalPlayerName[0] == '\0')
    SStrCopy(gLocalPlayerName, error_get_str(0x48), 25);
  return LaunchDone(LaunchReplay(pszPath), bWasExpansion);
}

int PortLaunchBenchReplay(const char *pszPath)
{
  const BYTE bWasExpansion = gbIsExpansion;
  return LaunchDone(LaunchReplay(pszPath), bWasExpansion);
}

int PortLaunchMapFile(const char *pszPath, bool bUms)
{
  const BYTE bWasExpansion = gbIsExpansion;
  if (gbMultiPlayerMode != 0)
    return FALSE;
  if (gLocalPlayerName[0] == '\0')
    SStrCopy(gLocalPlayerName, error_get_str(0x48), 25);
  bool bStarted;
  if (bUms) {
    bStarted = LaunchScenario(pszPath);
  } else {
    // a melee game as --map plays one, with the command line's race and computers
    static PortOptions s_options;
    s_options = *PortCommandOptions();
    SStrCopy(s_options.szMap, pszPath, sizeof s_options.szMap);
    s_options.bUms = false;
    s_options.bBench = false;
    bStarted = LaunchMap(&s_options);
  }
  g_bChannelStarting = bStarted;
  return LaunchDone(bStarted, bWasExpansion);
}

int PortLaunchCanvas(const char *pszTileset)
{
  BYTE bWasExpansion = gbIsExpansion;
  if (gbMultiPlayerMode != 0)
    return FALSE;
  if (gLocalPlayerName[0] == '\0')
    SStrCopy(gLocalPlayerName, error_get_str(0x48), 25);
  return LaunchDone(LaunchCanvas(pszTileset), bWasExpansion);
}
