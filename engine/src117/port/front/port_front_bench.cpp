// port_front_bench.cpp -- the benchmark's set-up. Presentation only: Start Benchmark hands its
// settings to port_bench.cpp (PortBenchFromMenus), which makes the run's game and plays its
// replay, and the menus come back on this screen when the run is over.
//
// The benchmark is a mod (mods\benchmark\mod.json, mode "bench") whose home screen button
// opens this screen. New Run is the set-up: five scenarios, the players (every slot a
// computer, in the skirmish's rows; the viewer an observer), the options, the map with its
// starts read from its file, the run to hold this one against, and the command line for the
// same run with no menus. Runs lists benchmarks\ in the data folder, newest first, and holds
// one against another, saying whether the two played the same game. Handicaps, computer
// levels and personalities, which 1.17's melee does not have, are greyed with a tip.
#include <windows.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <algorithm>
#include <string>
#include <vector>
#include <SDL3/SDL.h>
#include "../tools/port_bench.h"
#include "../platform/port_paths.h"  // PortDataFolder, PortMapFindInFolders
#include "../data/port_stormapi.h"     // SFileGetInstallPath
#include "../loop/port_perf.h"  // PERF_PARTS
#include "../render/port_present.h"  // PortPresentGpu
#include "../platform/port_util.h"  // PortLower
#include "port_frontkit.h"
#include "port_frontlobby.h"
#include "port_frontscreens.h"
#include "../render/port_gpu.h"     // PortGpuDriver
#include "../platform/port_json.h"
#include "../gameplay/port_teams.h"   // the layouts and the teams' shades
#include "../gameplay/port_ai.h"      // the computers' levels
#include "../gameplay/port_mutators.h"  // the supply cap, the unit limit and the mutators
#include "port_front_mutators.h"        // the Mutators tab
#include "../tools/port_benchoverlay.h"  // the Screen's styles and the overlay's flags
#include "port_ui.h"
#include "glues.h"        // mem_free
#include "maphdr.h"       // get_map_chk

using namespace FrontKit;

namespace {

const char *const kDash = "\xe2\x80\x94";

// bench.css
const UiColor kBmGood = KitRgb(0x7bd88f);
const UiColor kBmBad = KitRgb(0xff8a65);
const UiColor kBmThis = KitRgb(0x66d5fe);
const UiColor kBmVs = KitRgb(0xffe490);
const UiColor kInkWhite = KitRgb(0xffffff);
const UiColor kInkPlayer = KitRgb(0x64d0f9);
const UiColor kGoldRim = KitRgb(0xdaa308, 0.7f);
const UiColor kTrack = KitRgb(0x3980bc, 0.14f);
const UiColor kGrid = KitRgb(0x3980bc, 0.22f);
const UiColor kBand = KitRgb(0x66d5fe, 0.22f);

const int kPictureSlot = 13;  // the picture atlas's (the skirmish has 0, the campaign 1 to 12)

// The maps, Big Game Hunters remade at five sizes, read from maps\Hyperion\ (MapPath).
struct MapDef {
  const char *title;
  const char *shortName;
  const char *line;
};
// title and shortName are the map's own name, as its file is called; line is what it is
const MapDef kMaps[] = {
    {"Big Game Hunters 2x", "BGH 2x", PT_("Four copies, two by two")},
    {"Big Game Hunters 3x3", "BGH 3x3", PT_("Nine copies, three by three")},
    {"Big Game Hunters 4x4", "BGH 4x4", PT_("Sixteen copies, the four corner starts in each")},
    {"Big Game Hunters XL", "BGH XL", PT_("Sixteen copies, one start in each; the other bases are expansions")},
    {"Big Game Hunters 8x8", "BGH 8x8",
     PT_("Sixty-four copies, eight by eight, one start in each; the other bases are expansions")},
};
const int kMapCount = 5;
const char *const kTilesets[] = {PT_("Badlands"), PT_("Space Platform"), PT_("Installation"), PT_("Ash World"),
                                 PT_("Jungle"),   PT_("Desert"),         PT_("Arctic"),       PT_("Twilight")};

struct MapRead {
  bool bTried = false;
  bool bFound = false;
  std::string path;
  MapPicture pic;
  int nStarts = 0;
};
MapRead g_maps[kMapCount];

// their English names: the Mutators tab reads the frame's length from one, and each is
// translated where it shows
const char *const kSpeeds[] = {PT_("Slowest"), PT_("Slower"), PT_("Slow"),    PT_("Normal"),
                               PT_("Fast"),    PT_("Faster"), PT_("Fastest"), PT_("As fast as it goes")};
const char *const kSpeedIds[] = {"slowest", "slower", "slow", "normal", "fast", "faster", "fastest", "max"};
const int kSpeedCount = 8;

// minutes at Fastest, 42 ms a game frame, or frames
struct Length {
  const char *label;
  ULONG frames;
};
const Length kLengths[] = {{PT_("1 minute"), 1429},       {PT_("5 minutes"), 7143},
                           {PT_("10 minutes"), 14286},    {PT_("15 minutes"), 21429},
                           {PT_("30 minutes"), 42857},    {PT_("60 minutes"), 85714},
                           {PT_("10,000 frames"), 10000}, {PT_("50,000 frames"), 50000}};
const int kLengthCount = 8;

// a slot's races, as the skirmish lists them, and their letters (--bench-races)
const char *const kRaceNames[] = {PT_("Zerg"), PT_("Terran"), PT_("Protoss"), PT_("Random")};
const char kRaceLetters[] = "ztpr";

// the design's 32 player colours, the engine's order
const unsigned kColours[32] = {0xf40404, 0x0c48cc, 0x2cb494, 0x88409c, 0xf88c14, 0x703014, 0xcce0d0, 0xfcfc38,
                               0x10fc18, 0x7cacfc, 0x00e4fc, 0x006400, 0x4874b4, 0xfcfc00, 0x683078, 0xbca45c,
                               0xfccc2c, 0x486c50, 0x18acb8, 0x088008, 0xffffff, 0x2424fc, 0xfc9468, 0xbc6824,
                               0xf40404, 0xac8c7c, 0x004400, 0xecc4b0, 0x4068d4, 0x344c6c, 0x58b058, 0x7c5050};

// port_perf.cpp's parts, the first eight the game frame's, the last three the picture's
const UiColor kPartColours[PERF_PARTS] = {KitRgb(0x8fb8de), KitRgb(0xb0705a), KitRgb(0xe6b450), KitRgb(0x6f8fbf),
                                          KitRgb(0x4fc3d9), KitRgb(0x7bd88f), KitRgb(0xc792ea), KitRgb(0x9aa6b2),
                                          KitRgb(0x5fb3a1), KitRgb(0xd6d26b), KitRgb(0xf07178)};
const char *const kPartNotes[PERF_PARTS] = {
    PT_("input and the game\xe2\x80\x99s events"), PT_("creep spreading and receding"),
    PT_("the computers\xe2\x80\x99 scripts"),      PT_("every player\xe2\x80\x99s vision"),
    PT_("every unit\xe2\x80\x99s orders and state"), PT_("pathing and moving"),
    PT_("the map\xe2\x80\x99s triggers"),          PT_("the state check a replay compares"),
    PT_("the minimap"),                            PT_("the console and this overlay"),
    PT_("the map and units")};

// The scenarios: each sets the map, the players, the speed and the length. Extreme is the
// largest game Hyperion holds, 64 players on 1,024 tiles a side, in place of the mod's card the
// design had (the user, 2026-10-26: "Instead update it to do the 64 player size count along with
// the increase 1024x1024 map size").
struct Scenario {
  const char *name;
  const char *foot;
  int nMap, nPlayers, nSpeed, nLength;
};
const Scenario kScenarios[] = {{PT_("Quick check"), PT_("A minute, to see it runs"), 0, 8, 7, 0},
                               {PT_("Standard"), PT_("The one to compare builds by"), 1, 16, 6, 2},
                               {PT_("Stress"), PT_("The most the scope allows"), 2, 32, 7, 3},
                               {PT_("Soak"), PT_("An hour, for leaks and drift"), 3, 16, 6, 5},
                               {PT_("Extreme"), PT_("The largest game Hyperion holds"), 4, 64, 7, 4}};
const int kScenarioCount = 5;
const int kSeats = 64;  // the most players the set-up seats

// The set-up's choices.
struct SetUp {
  int nMap = 1;
  int nPlayers = 16;
  std::string seats = std::string(kSeats, 'r');  // each slot's race: t, z, p, or r drawn from the seed
  std::string levels = std::string(kSeats, 'c');  // each slot's computer level, AiLevelLetter's
  int nSpeed = 6;
  int nLength = 2;           // kLengths, or kLengthCount for a custom length
  int nCustomMinutes = 90;   // the custom length, 1 minute to 24 hours
  unsigned nSeed = 20261015;
  bool bFramesCsv = true;
  int nShown = 0;        // in a window, full screen, no picture
  PortBenchOverlay overlay;  // the run's overlay, which the Screen's style sets (port_benchoverlay.cpp)
  int nFramesShown = 0;  // as many as it can, held to the display
  int nPicture = 0;      // the GPU's, 1.17's 256 colours
  int nGameType = 0;     // Free For All, 1.17's melee (every computer on one side), Team Match
  int nStartsBy = 0;     // starts drawn from the seed, or by slot: player n at the map's start n
  int nColourBy = 0;     // by player, or (Team Match) each team in shades of its colour
  // the mutators (port_mutators.h): the Mutators tab's, and with them the supply cap, 0 for
  // none, and the unit limit, 0 Automatic, which the options set too; any but the
  // original's plays by Hyperion's rules
  MutatorSet mut;
  // Team Match: each slot's team from 0, the teams' names, and the quick layout that set them
  // ("" once changed by hand), for the map and count in nTeamsFor (map * 128 + players)
  std::vector<int> teams = std::vector<int>(kSeats, 0);
  std::vector<std::string> teamNames;
  std::string layout;
  int nTeamsFor = -1;
};
SetUp g_s;
int g_nSeatFirst;      // the first slot shown in the players' list
float g_fSeatRoom;     // the list's height for rows and team heads, measured each frame
std::string g_hours, g_minutes;  // the custom length as typed
int g_nEdit;                     // the field with the keyboard: 0 none, 1 hours, 2 minutes
int g_nEditWas;                  // the field that had it last frame
bool g_bFresh;                   // the field was just given the keyboard: a key replaces its text

// The most game frames a run goes: a preset's, or the custom minutes at Fastest's 42 ms.
ULONG LengthFrames(void)
{
  if (g_s.nLength < kLengthCount)
    return kLengths[g_s.nLength].frames;
  return static_cast<ULONG>(lround(g_s.nCustomMinutes * 60000.0 / 42.0));
}

// Minutes as the set-up says them: "45 minutes", "2 hours", "1 hour 30 minutes".
std::string MinutesLabel(int nMinutes)
{
  const int h = nMinutes / 60, m = nMinutes % 60;
  std::string s;
  if (h > 0)
    s = PortTextNF("{0} hour", "{0} hours", h, {std::to_string(h)});
  if (m > 0 || h == 0)
    s += (h > 0 ? " " : "") + PortTextNF("{0} minute", "{0} minutes", m, {std::to_string(m)});
  return s;
}

// The custom length's fields from the minutes that count.
void LengthFields(void)
{
  g_hours = std::to_string(g_s.nCustomMinutes / 60);
  g_minutes = std::to_string(g_s.nCustomMinutes % 60);
}

const char *const kShownIds[] = {"window", "full", "none"};
const char *const kPictureIds[] = {"gpu", "256"};

// A run, read from its folder under benchmarks\.
struct Run {
  std::string path, folder;
  std::string map, title, when, commit, branch, speed, race, races, ended, endState, renderer;
  std::string shown = "window", framesShown = "free", picture = "gpu";
  PortBenchOverlay overlay;  // settings.json's, or the panels for a run before the overlay's styles
  std::string overlayKey;    // what the runs compared must share (PortBenchOverlayKey)
  std::string gameType = "melee";  // a run before the game type was a melee
  std::string teams, layout, starts, colours;
  std::string levels;  // --ai-levels, "" for every computer Classic
  // settings.json's mutators, and in words, "" for none; the unit table the game had, 0
  // before a run wrote it
  MutatorSet mut;
  std::string mutators;
  int unitTable = 0;
  int players = 0, seed = 0;
  ULONG frames = 0;
  bool bSummary = false, bFramesCsv = false, bReplay = false;
  double fps = 0, fpsWorst = 0, gfps = 0, seconds = 0;
  unsigned gameFrames = 0, most = 0, mostAt = 0;
  double parts[PERF_PARTS] = {};
  // seconds.csv, read when the run is shown: the game frame reached and the mean frame time
  bool bSecondsRead = false;
  std::vector<float> secFrame, secMs, secUnits;
};
std::vector<Run> g_runs;
int g_nPick = -1;
int g_nAgainst = -2;  // the run held against in Runs: -2 the one found, -1 none
int g_nFirst;         // the list's first row shown
int g_nShownRows = 12;

int g_nTab;        // 0 New Run, 1 Runs
int g_nView;       // New Run's right columns: 0 the options, 1 the Mutators tab
int g_nResult;     // what the screen chose (port_frontscreens.h)
bool g_bPending;   // Start Benchmark pressed: the settings wait for PortFrontStartMode
PortBenchSettings g_pending;
std::string g_note;  // a line under the title, for what cannot be done or was done
Uint64 g_nNoteAt;
std::string g_lastSeen;  // the last run the screen came back to

std::string Commas(ULONG n)
{
  std::string s = std::to_string(n);
  for (int i = static_cast<int>(s.size()) - 3; i > 0; i -= 3)
    s.insert(static_cast<size_t>(i), ",");
  return s;
}

std::string Clock(double dSeconds)
{
  const int n = static_cast<int>(dSeconds + 0.5);
  char sz[32];
  if (n >= 3600)
    snprintf(sz, sizeof sz, "%d:%02d:%02d", n / 3600, n / 60 % 60, n % 60);
  else
    snprintf(sz, sizeof sz, "%d:%02d", n / 60, n % 60);
  return sz;
}

std::string Fixed1(double d)
{
  char sz[32];
  snprintf(sz, sizeof sz, "%.1f", d);
  return sz;
}

void Note(const std::string &s)
{
  g_note = s;
  g_nNoteAt = SDL_GetTicks();
}

// ---------------------------------------------------------------------------
// The maps.

// A map's file, maps\Hyperion\<title>.hxm or in its Experimental\ folder, where a package files
// a map past 32 players or 512 tiles (Big Game Hunters 4x4): in the data folder, else in the
// game's folder, where a package puts them beside the data folder, else in a --maps folder. The
// data folder's path when it is in none, for the note that says so.
std::string MapPath(const char *pszTitle)
{
  const std::string names[2] = {std::string("maps\\Hyperion\\") + pszTitle + ".hxm",
                                std::string("maps\\Hyperion\\Experimental\\") + pszTitle + ".hxm"};
  char szGame[MAX_PATH] = "";
  const bool bGame = SFileGetInstallPath(szGame, sizeof szGame, 1) != 0;
  for (const std::string &name : names) {
    const std::string data = std::string(PortDataFolder()) + name;
    if (GetFileAttributesA(data.c_str()) != INVALID_FILE_ATTRIBUTES)
      return data;
    const std::string game = std::string(szGame) + name;
    if (bGame && GetFileAttributesA(game.c_str()) != INVALID_FILE_ATTRIBUTES)
      return game;
    char sz[MAX_PATH] = "";
    if (PortMapFindInFolders(name.c_str(), sz, sizeof sz))
      return sz;
  }
  return std::string(PortDataFolder()) + names[0];
}

MapRead &MapOf(int i)
{
  MapRead &m = g_maps[i];
  if (m.bTried)
    return m;
  m.bTried = true;
  m.path = MapPath(kMaps[i].title);
  if (GetFileAttributesA(m.path.c_str()) == INVALID_FILE_ATTRIBUTES)
    return m;
  DWORD dwSize = 0;
  BYTE *pChk = static_cast<BYTE *>(get_map_chk(m.path.c_str(), &dwSize));
  if (pChk == NULL)
    return m;
  m.pic = MapPicture();
  m.pic.nSlot = kPictureSlot;
  MapPictureRead(&m.pic, m.path, pChk, dwSize);
  mem_free(pChk, __FILE__, __LINE__);
  m.bFound = true;
  m.nStarts = static_cast<int>(m.pic.starts.size());
  return m;
}

// the picture atlas's slot holds one map's minimap: read again when another is shown
int g_nPictureOf = -1;
void ShowMap(int i)
{
  if (g_nPictureOf == i)
    return;
  g_nPictureOf = i;
  MapRead &m = MapOf(i);
  if (!m.bFound)
    return;
  DWORD dwSize = 0;
  BYTE *pChk = static_cast<BYTE *>(get_map_chk(m.path.c_str(), &dwSize));
  if (pChk == NULL)
    return;
  m.pic = MapPicture();
  m.pic.nSlot = kPictureSlot;
  MapPictureRead(&m.pic, m.path, pChk, dwSize);
  mem_free(pChk, __FILE__, __LINE__);
}

// the most players the map seats, 32 when it could not be read
int MapStarts(int i)
{
  const MapRead &m = MapOf(i);
  return m.bFound && m.nStarts > 0 ? m.nStarts : 32;
}

void FitPlayers(void)
{
  const int n = MapStarts(g_s.nMap);
  if (g_s.nPlayers > n)
    g_s.nPlayers = n >= 64 ? 64 : n >= 32 ? 32 : n >= 16 ? 16 : 8;
}

// ---------------------------------------------------------------------------
// The set-up's races and the settings it hands on.

char SeatLetter(int i)
{
  const char c = i < static_cast<int>(g_s.seats.size()) ? g_s.seats[i] : 'r';
  return c == 't' || c == 'z' || c == 'p' ? c : 'r';
}

// The races as the command line and settings.json name them: one word when every slot plays
// the same, else a letter a slot.
std::string RacesWord(void)
{
  std::string s;
  bool bSame = true;
  for (int i = 0; i < g_s.nPlayers; ++i) {
    s += SeatLetter(i);
    bSame = bSame && s[i] == s[0];
  }
  if (!bSame || s.empty())
    return s;
  return s[0] == 't' ? "terran" : s[0] == 'z' ? "zerg" : s[0] == 'p' ? "protoss" : "random";
}

bool OneRace(void)
{
  const std::string w = RacesWord();
  return w == "random" || w == "terran" || w == "zerg" || w == "protoss";
}

// The map's starts read for every player: starts by slot and the quick layouts need them.
bool StartsKnown(void)
{
  const MapRead &m = MapOf(g_s.nMap);
  return m.bFound && m.pic.w > 0 && static_cast<int>(m.pic.starts.size()) >= g_s.nPlayers;
}

// The quick layouts for the run's players: the map's first starts, one a player, each playing
// from its own (MapLayouts).
const std::vector<TeamLayout> &Layouts(void)
{
  return MapLayouts(StartsKnown() ? &MapOf(g_s.nMap).pic : NULL, g_s.nPlayers);
}

bool TeamMatch(void)
{
  return g_s.nGameType == 2;
}

int TeamsKey(void)
{
  return g_s.nMap * 128 + g_s.nPlayers;
}

// A quick layout: its teams and their names, and starts by slot, so each team plays from its
// part of the map.
void ApplyLayout(const TeamLayout &l)
{
  for (int i = 0; i < g_s.nPlayers && i < static_cast<int>(l.teams.size()); ++i)
    g_s.teams[i] = l.teams[i];
  g_s.teamNames = l.names;
  g_s.layout = l.name;
  g_s.nStartsBy = 1;
  g_s.nTeamsFor = TeamsKey();
}

// Team Match's teams for the map and count: the quick layout of the same name, else the
// first, else the slots split in two.
void FirstTeams(void)
{
  const std::vector<TeamLayout> &ls = Layouts();
  const TeamLayout *pl = NULL;
  for (const TeamLayout &l : ls)
    if (!l.bFfa && l.name == g_s.layout)
      pl = &l;
  for (size_t k = 0; pl == NULL && k < ls.size(); ++k)
    if (!ls[k].bFfa)
      pl = &ls[k];
  if (pl != NULL) {
    ApplyLayout(*pl);
    return;
  }
  for (int i = 0; i < kSeats; ++i)
    g_s.teams[i] = i >= (g_s.nPlayers + 1) / 2 ? 1 : 0;
  g_s.teamNames = {"Team 1", "Team 2"};
  g_s.layout.clear();
  g_s.nTeamsFor = TeamsKey();
}

// Each frame: Team Match's teams fit the map and the count chosen, and starts by slot have
// the map's starts to go by.
void SyncTeams(void)
{
  if (TeamMatch() && (g_s.nTeamsFor != TeamsKey() || g_s.teamNames.empty()))
    FirstTeams();
  if (g_s.nStartsBy == 1 && !StartsKnown())
    g_s.nStartsBy = 0;
}

// Slot i joins team q, a new one when q is past the last; the teams are then by hand.
void JoinTeam(int i, int q)
{
  if (q >= static_cast<int>(g_s.teamNames.size()))
    q = TeamsAdd(&g_s.teamNames);
  g_s.teams[i] = q;
  g_s.layout.clear();
}

// The teams as --teams has them, a player each from 1, "" outside Team Match.
std::string TeamsWord(void)
{
  std::string s;
  for (int i = 0; TeamMatch() && i < g_s.nPlayers; ++i)
    s += (i ? "," : "") + std::to_string(g_s.teams[i] + 1);
  return s;
}

// The starts as --starts has them: by slot, player n at the map's start n; "" when the seed
// draws them.
// --ai-levels: a letter a player (AiLevelLetter), "" when every one is Classic.
std::string LevelsWord(void)
{
  std::string s;
  bool bOther = false;
  for (int i = 0; i < g_s.nPlayers && i < kSeats; ++i) {
    s += (i ? "," : "") + std::string(1, g_s.levels[i]);
    bOther = bOther || g_s.levels[i] != 'c';
  }
  return bOther ? s : std::string();
}

const char *const kLevelTip = PT_(
    "Classic AI is the original\xe2\x80\x99s own computer. The others play by Hyperion\xe2\x80\x99s rules: StarCraft "
    "II\xe2\x80\x99s levels, Very Easy to Elite, from slower and smaller to faster, and the Cheater levels, "
    "which play as Elite and cheat as StarCraft II\xe2\x80\x99s do. Vision sees the whole map; resources sees "
    "it too and brings back more from each trip, growing to half as much again by 20 minutes; insane "
    "brings back a fifth more from the start, growing to twice as much.");

std::string StartsWord(void)
{
  std::string s;
  if (g_s.nStartsBy != 1 || !StartsKnown())
    return s;
  for (int i = 0; i < g_s.nPlayers; ++i)
    s += (i ? "," : "") + std::to_string(MapOf(g_s.nMap).pic.starts[i].nSlot);
  return s;
}

bool ColoursByTeam(void)
{
  return TeamMatch() && g_s.nColourBy == 1;
}

// Each player's colour: by team its team's shade, in slot order within the team; else the
// engine's colour for the slot.
unsigned SeatColour(int i)
{
  if (!ColoursByTeam())
    return kColours[i % 32];
  const int q = g_s.teams[i];
  int nIn = 0, k = 0;
  for (int j = 0; j < g_s.nPlayers; ++j)
    if (g_s.teams[j] == q) {
      if (j < i)
        ++k;
      ++nIn;
    }
  return TeamsShades(kTeamBases[q % 16], nIn)[k];
}

// The teams in a line: the quick layout's name, or by hand, and their sizes.
std::string TeamsLine(void)
{
  const std::vector<int> t(g_s.teams.begin(), g_s.teams.begin() + g_s.nPlayers);
  return (g_s.layout.empty() ? std::string(PortText("Teams by hand")) : g_s.layout) + kDot + TeamsVs(t);
}

// The set-up's mutators as --mutators lists them, "" for the original's.
std::string MutatorsWord(void)
{
  return MutatorsList(g_s.mut);
}

// The same in words, as a run's settings.json reads back (MutatorsText).
std::string MutatorsSaid(void)
{
  return MutatorsText(g_s.mut);
}

PortBenchSettings Settings(void)
{
  PortBenchSettings b;
  lstrcpynA(b.szMap, MapOf(g_s.nMap).path.c_str(), sizeof b.szMap);
  b.nPlayers = g_s.nPlayers;
  if (OneRace()) {
    lstrcpynA(b.szRace, RacesWord().c_str(), sizeof b.szRace);
  } else {
    lstrcpynA(b.szRace, "random", sizeof b.szRace);
    lstrcpynA(b.szRaces, RacesWord().c_str(), sizeof b.szRaces);
  }
  b.nSeed = static_cast<int>(g_s.nSeed);
  b.dwFrames = LengthFrames();
  b.nSpeed = g_s.nSpeed < 7 ? g_s.nSpeed : 6;
  b.nSpeedMul = g_s.nSpeed < 7 ? 1 : 0;
  b.bFramesCsv = g_s.bFramesCsv;
  b.nShown = g_s.nShown;
  b.overlay = g_s.overlay;
  b.nFramesShown = g_s.nFramesShown;
  b.nPicture = g_s.nPicture;
  b.nGameType = g_s.nGameType == 1 ? 1 : 0;  // Team Match plays on Free For All's template
  lstrcpynA(b.szStarts, StartsWord().c_str(), sizeof b.szStarts);
  lstrcpynA(b.szAiLevels, LevelsWord().c_str(), sizeof b.szAiLevels);
  lstrcpynA(b.szMutators, MutatorsWord().c_str(), sizeof b.szMutators);
  if (TeamMatch()) {
    lstrcpynA(b.szTeams, TeamsWord().c_str(), sizeof b.szTeams);
    lstrcpynA(b.szLayout, g_s.layout.c_str(), sizeof b.szLayout);
  }
  if (ColoursByTeam()) {
    std::string colours;
    for (int i = 0; i < g_s.nPlayers; ++i) {
      char sz[16];
      snprintf(sz, sizeof sz, "%s#%06x", i ? "," : "", SeatColour(i));
      colours += sz;
    }
    lstrcpynA(b.szColours, colours.c_str(), sizeof b.szColours);
  }
  return b;
}

std::string CommandLine(void)
{
  std::string s = "Hyperion.exe --bench --map \"" + MapOf(g_s.nMap).path + "\" --players " +
                  std::to_string(g_s.nPlayers);
  s += OneRace() ? " --computer-race " + RacesWord() : " --bench-races " + RacesWord();
  s += " --seed " + std::to_string(g_s.nSeed) + " --bench-frames " + std::to_string(LengthFrames()) +
       " --bench-speed " + kSpeedIds[g_s.nSpeed];
  if (g_s.bFramesCsv)
    s += " --bench-frames-csv";
  if (g_s.nGameType == 1)
    s += " --bench-type melee";
  if (TeamMatch())
    s += " --teams " + TeamsWord();
  const std::string starts = StartsWord();
  if (!starts.empty())
    s += " --starts " + starts;
  const std::string levels = LevelsWord();
  if (!levels.empty())
    s += " --ai-levels " + levels;
  const std::string mutators = MutatorsWord();
  if (!mutators.empty())
    s += " --mutators " + mutators;
  if (g_s.nShown != 0)
    s += std::string(" --bench-shown ") + kShownIds[g_s.nShown];
  s += PortBenchOverlayArgs(g_s.nShown, g_s.overlay);
  if (g_s.nFramesShown != 0)
    s += " --bench-frames-shown display";
  if (g_s.nPicture != 0)
    s += " --bench-picture 256";
  return s;
}

std::string Stamp(void)
{
  SYSTEMTIME t;
  GetLocalTime(&t);
  char sz[32];
  snprintf(sz, sizeof sz, "%04u-%02u-%02u %02u%02u", t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute);
  return sz;
}

int ScenarioPressed(void)
{
  for (int i = 0; i < kScenarioCount; ++i) {
    const Scenario &c = kScenarios[i];
    if (c.nMap == g_s.nMap && c.nPlayers == g_s.nPlayers && c.nSpeed == g_s.nSpeed && c.nLength == g_s.nLength)
      return i;
  }
  return -1;
}

// ---------------------------------------------------------------------------
// The runs.

void ReadRun(Run &r)
{
  PortJson j;
  if (PortJsonParse(PortReadText(r.path + "\\settings.json").c_str(), &j)) {
    r.map = j["map"].Str();
    r.title = j["title"].Str();
    if (r.title.empty()) {
      const size_t nSlash = r.map.find_last_of("\\/");
      r.title = r.map.substr(nSlash == std::string::npos ? 0 : nSlash + 1);
      const size_t nDot = r.title.rfind('.');
      if (nDot != std::string::npos)
        r.title.resize(nDot);
    }
    r.players = static_cast<int>(j["players"].Num());
    r.race = j["race"].Str("random");
    r.races = j["races"].Str();
    r.seed = static_cast<int>(j["seed"].Num(1));
    r.frames = static_cast<ULONG>(j["frames"].Num());
    r.speed = j["speed"].Str("fastest");
    r.when = j["when"].Str();
    r.commit = j["commit"].Str();
    r.branch = j["branch"].Str();
    r.renderer = j["renderer"].Str();
    // a run before the overlay filled the window, which settings.json called full
    r.shown = j["shown"].Str("window");
    if (j["frames_shown"].kind != PortJson::STRING && r.shown == "full")
      r.shown = "window";
    r.framesShown = j["frames_shown"].Str("free");
    r.picture = j["picture"].Str("gpu");
    PortBenchOverlayFromJson(j["overlay"], &r.overlay);
    r.overlayKey = PortBenchOverlayKey(r.shown == "full" ? 1 : r.shown == "none" ? 2 : 0, r.overlay);
    r.gameType = j["game_type"].Str("melee");
    r.teams = j["teams"].Str();
    r.layout = j["layout"].Str();
    r.starts = j["starts"].Str();
    r.colours = j["colours"].Str();
    r.levels = j["ai_levels"].Str();
    if (j["mutators"].kind == PortJson::OBJECT) {
      r.mut = MutatorsFromJson(j["mutators"]);
      r.mutators = MutatorsText(r.mut);
    }
    r.unitTable = static_cast<int>(j["unit_table"].Num());
    r.bFramesCsv = j["frames_csv"].kind == PortJson::BOOL && j["frames_csv"].b;
  }
  // the earlier runs' folders were named 20261015_074706_<map>_<n>p
  if (r.when.empty() && r.folder.size() >= 15 && r.folder[8] == '_')
    r.when = r.folder.substr(0, 4) + "-" + r.folder.substr(4, 2) + "-" + r.folder.substr(6, 2) + " " +
             r.folder.substr(9, 2) + ":" + r.folder.substr(11, 2);
  PortJson s;
  if (PortJsonParse(PortReadText(r.path + "\\summary.json").c_str(), &s) && s.kind == PortJson::OBJECT) {
    r.bSummary = true;
    r.ended = s["ended"].Str();
    r.seconds = s["seconds"].Num();
    r.fps = s["frames_per_s"].Num();
    r.fpsWorst = s["frames_per_s_1pct_low"].Num();
    r.gfps = s["game_frames_per_s"].Num();
    r.gameFrames = static_cast<unsigned>(s["game_frames"].Num());
    r.most = static_cast<unsigned>(s["units_most"].Num());
    r.mostAt = static_cast<unsigned>(s["units_most_at"].Num());
    r.endState = s["end_state"].Str();
    const PortJson &p = s["ms_per_game_frame"];
    for (int i = 0; i < PERF_PARTS; ++i)
      r.parts[i] = p[PortBenchPartName(i)].Num();
  }
  r.bReplay = GetFileAttributesA((r.path + "\\run.rep").c_str()) != INVALID_FILE_ATTRIBUTES;
}

void ReadSeconds(Run &r)
{
  if (r.bSecondsRead)
    return;
  r.bSecondsRead = true;
  const std::string text = PortReadText(r.path + "\\seconds.csv");
  size_t at = text.find('\n');
  double dLast = 0.0, dFrame = 0.0;
  while (at != std::string::npos && at + 1 < text.size()) {
    const size_t next = text.find('\n', at + 1);
    const std::string line = text.substr(at + 1, next == std::string::npos ? std::string::npos : next - at - 1);
    at = next;
    double d[4] = {};
    if (sscanf_s(line.c_str(), "%lf,%lf,%lf,%lf", &d[0], &d[1], &d[2], &d[3]) != 4)
      continue;
    dFrame += d[2] * (d[0] - dLast);
    dLast = d[0];
    r.secFrame.push_back(static_cast<float>(dFrame));
    r.secMs.push_back(d[1] > 0.0 ? static_cast<float>(1000.0 / d[1]) : 0.0f);
    r.secUnits.push_back(static_cast<float>(d[3]));
  }
}

void ListRuns(const std::string &pick)
{
  g_runs.clear();
  const std::string root = std::string(PortDataFolder()) + "benchmarks";
  WIN32_FIND_DATAA fd;
  HANDLE h = FindFirstFileA((root + "\\*").c_str(), &fd);
  if (h != INVALID_HANDLE_VALUE) {
    do {
      if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || fd.cFileName[0] == '.')
        continue;
      Run r;
      r.folder = fd.cFileName;
      r.path = root + "\\" + fd.cFileName;
      ReadRun(r);
      g_runs.push_back(r);
    } while (FindNextFileA(h, &fd));
    FindClose(h);
  }
  std::stable_sort(g_runs.begin(), g_runs.end(), [](const Run &a, const Run &b) {
    return a.when != b.when ? a.when > b.when : a.folder > b.folder;
  });
  g_nPick = g_runs.empty() ? -1 : 0;
  for (size_t i = 0; i < g_runs.size(); ++i)
    if (!pick.empty() && lstrcmpiA(g_runs[i].path.c_str(), pick.c_str()) == 0)
      g_nPick = static_cast<int>(i);
  g_nAgainst = -2;
  g_nFirst = 0;
}

std::string RunRaces(const Run &r)
{
  return r.races.empty() ? PortLower(r.race) : r.races;
}

// The same game: the same map, players, races and seed, at the same speed for the same length.
// shown the same way: the renderer, the picture, the shown mode and the frames shown
bool SameShown(const Run &a, const Run &b)
{
  return a.renderer == b.renderer && a.picture == b.picture && a.shown == b.shown && a.framesShown == b.framesShown &&
         a.overlayKey == b.overlayKey;
}

bool SameGame(const Run &a, const Run &b)
{
  return lstrcmpiA(a.map.c_str(), b.map.c_str()) == 0 && a.players == b.players && RunRaces(a) == RunRaces(b) &&
         a.seed == b.seed && a.speed == b.speed && a.frames == b.frames && a.gameType == b.gameType &&
         a.teams == b.teams && a.starts == b.starts && a.levels == b.levels && a.mutators == b.mutators;
}

// The run a run is held against when none is picked: the latest earlier run of the same game
// with the same renderer.
int Baseline(int i)
{
  if (i < 0 || i >= static_cast<int>(g_runs.size()))
    return -1;
  for (size_t k = i + 1; k < g_runs.size(); ++k)
    if (g_runs[k].bSummary && SameGame(g_runs[i], g_runs[k]) && SameShown(g_runs[i], g_runs[k]))
      return static_cast<int>(k);
  return -1;
}

// The run the set-up's game would be held against.
int SetUpBaseline(void)
{
  Run now;
  now.map = MapOf(g_s.nMap).path;
  now.players = g_s.nPlayers;
  now.race = OneRace() ? RacesWord() : "random";
  now.races = OneRace() ? "" : RacesWord();
  now.seed = static_cast<int>(g_s.nSeed);
  now.speed = kSpeedIds[g_s.nSpeed];
  now.frames = LengthFrames();
  now.renderer = PortPresentGpu() ? PortGpuDriver() : "sdl";
  now.shown = kShownIds[g_s.nShown];
  now.overlayKey = PortBenchOverlayKey(g_s.nShown, g_s.overlay);
  now.framesShown = g_s.nFramesShown == 1 ? "display" : "free";
  now.picture = kPictureIds[g_s.nPicture];
  now.gameType = g_s.nGameType == 1 ? "melee" : "ffa";
  now.teams = TeamsWord();
  now.starts = StartsWord();
  now.levels = LevelsWord();
  now.mutators = MutatorsSaid();
  for (size_t k = 0; k < g_runs.size(); ++k)
    if (g_runs[k].bSummary && SameGame(now, g_runs[k]) && SameShown(now, g_runs[k]))
      return static_cast<int>(k);
  return -1;
}

int SpeedIndex(const std::string &id)
{
  for (int i = 0; i < kSpeedCount; ++i)
    if (id == kSpeedIds[i])
      return i;
  return 6;
}

std::string LengthLabel(ULONG frames)
{
  for (const Length &l : kLengths)
    if (l.frames == frames)
      return PortText(l.label);
  // a custom length, in whole minutes at Fastest
  const int nMinutes = static_cast<int>(lround(frames * 42.0 / 60000.0));
  if (nMinutes > 0 && static_cast<ULONG>(lround(nMinutes * 60000.0 / 42.0)) == frames)
    return MinutesLabel(nMinutes);
  return PortTextF("{0} frames", {Commas(frames)});
}

std::string RunScenario(const Run &r)
{
  std::string map = r.title;
  for (const MapDef &m : kMaps)
    if (map == m.title)
      map = m.shortName;
  std::string s = map + kDot + PortTextNF("{0} player", "{0} players", r.players, {std::to_string(r.players)}) + kDot +
                  PortText(kSpeeds[SpeedIndex(r.speed)]) + kDot + LengthLabel(r.frames);
  if (r.gameType == "melee")
    s += std::string(kDot) + PortText("melee, allied");
  if (!r.teams.empty())
    // {0} the quick layout, or that the teams are by hand
    s += std::string(kDot) + PortTextF("Team Match, {0}", {r.layout.empty() ? PortText("teams by hand") : r.layout});
  else if (!r.starts.empty())
    s += std::string(kDot) + PortText("starts by slot");
  if (!r.mutators.empty())
    s += std::string(kDot) + MutatorsText(r.mut, true);
  if (r.picture == "256")
    s += std::string(kDot) + PortText("256 colours");
  if (r.shown == "none")
    s += std::string(kDot) + PortText("no picture");
  if (r.ended == "stopped")
    s += std::string(kDot) + PortText("stopped");
  else if (r.ended == "last")
    s += std::string(kDot) + PortText("one player left");
  if (!r.bSummary)
    s += std::string(kDot) + PortText("no summary");
  return s;
}

std::string ShortWhen(const Run &r)
{
  return r.when.size() >= 16 ? r.when.substr(5) : r.when;
}

// Run Again: the run's settings back in the set-up.
bool TakeRun(const Run &r)
{
  int nMap = -1;
  for (int i = 0; i < kMapCount; ++i)
    if (lstrcmpiA(MapOf(i).path.c_str(), r.map.c_str()) == 0 || r.title == kMaps[i].title)
      nMap = i;
  if (nMap < 0) {
    Note(PortText("That run\xe2\x80\x99s map is not one of the set-up\xe2\x80\x99s: run it again from its command line."));
    return false;
  }
  g_s.nMap = nMap;
  g_s.nPlayers = r.players == 8 || r.players == 16 || r.players == 32 || r.players == 64 ? r.players : 16;
  const std::string races = RunRaces(r);
  const char cAll = races == "terran" ? 't' : races == "zerg" ? 'z' : races == "protoss" ? 'p' : 'r';
  g_s.seats.assign(kSeats, cAll);
  if (races != "terran" && races != "zerg" && races != "protoss" && races != "random")
    for (size_t i = 0; i < races.size() && i < kSeats; ++i)
      g_s.seats[i] = races[i];
  g_s.nSpeed = SpeedIndex(r.speed);
  // a preset's length, else a custom one in whole minutes
  g_s.nLength = kLengthCount;
  g_s.nCustomMinutes = static_cast<int>(lround(r.frames * 42.0 / 60000.0));
  g_s.nCustomMinutes = g_s.nCustomMinutes < 1 ? 1 : g_s.nCustomMinutes > 24 * 60 ? 24 * 60 : g_s.nCustomMinutes;
  LengthFields();
  for (int i = 0; i < kLengthCount; ++i)
    if (kLengths[i].frames == r.frames)
      g_s.nLength = i;
  g_s.nSeed = static_cast<unsigned>(r.seed);
  g_s.nGameType = r.gameType == "melee" ? 1 : r.teams.empty() ? 0 : 2;
  g_s.nStartsBy = r.starts.empty() ? 0 : 1;
  g_s.nColourBy = r.colours.empty() ? 0 : 1;
  if (!r.teams.empty()) {
    // the run's teams; their names the quick layout's when it is still the map's, else Team n
    int nTeams = 0;
    g_s.teams.assign(kSeats, 0);
    const char *p = r.teams.c_str();
    for (int i = 0; i < kSeats && *p != '\0'; ++i) {
      const int q = atoi(p) - 1;
      g_s.teams[i] = q >= 0 && q < kSeats ? q : 0;
      nTeams = (std::max)(nTeams, g_s.teams[i] + 1);
      while (*p != '\0' && *p != ',')
        ++p;
      if (*p == ',')
        ++p;
    }
    g_s.layout.clear();
    g_s.teamNames.clear();
    for (const TeamLayout &l : Layouts())
      if (!l.bFfa && l.name == r.layout && static_cast<int>(l.names.size()) >= nTeams) {
        g_s.layout = l.name;
        g_s.teamNames = l.names;
      }
    while (static_cast<int>(g_s.teamNames.size()) < nTeams)
      TeamsAdd(&g_s.teamNames);
    g_s.nTeamsFor = TeamsKey();
  }
  g_s.mut = r.mut;
  g_s.levels.assign(kSeats, 'c');
  for (size_t i = 0, k = 0; i < r.levels.size() && k < kSeats; ++i)
    if (AiLevelFromLetter(r.levels[i]) >= 0)
      g_s.levels[k++] = r.levels[i];
  g_s.nShown = r.shown == "full" ? 1 : r.shown == "none" ? 2 : 0;
  g_s.overlay = r.overlay;
  g_s.nFramesShown = r.framesShown == "display" ? 1 : 0;
  g_s.nPicture = r.picture == "256" ? 1 : 0;
  return true;
}

void Start(void)
{
  if (!MapOf(g_s.nMap).bFound) {
    // {0} the map's name
    Note(PortTextF("No maps\\Hyperion\\{0}.hxm in the game\xe2\x80\x99s folder or the data folder.",
                   {kMaps[g_s.nMap].title}));
    return;
  }
  SyncTeams();
  g_pending = Settings();
  // the earlier run of the same game, which the run's overlay holds this one against
  const int k = SetUpBaseline();
  if (k >= 0)
    lstrcpynA(g_pending.szAgainst, g_runs[k].path.c_str(), sizeof g_pending.szAgainst);
  g_bPending = true;
  g_nResult = FRONT_BENCH_STARTED;
  FrontLog("bench: start %s\n", CommandLine().c_str());
}

// ---------------------------------------------------------------------------
// Drawing pieces.

void DrawSwatch(float x, float y, float w, float h, void *pCtx)
{
  const ScDraw &d = *static_cast<const ScDraw *>(pCtx);
  UiFill(x - Sc(1), y - Sc(1), w + Sc(2), h + Sc(2), KitRgb(0x000000, 0.85f), Sc(3));
  UiFill(x, y, w, h, d.look.rim, Sc(2));
}

void Swatch(unsigned rgb, float fSide)
{
  Decl s;
  s.d.layout.sizing.width = Fixed(Sc(fSide));
  s.d.layout.sizing.height = Fixed(Sc(fSide));
  const UiColor c = KitRgb(rgb);
  const ScLook look = {c, c, c, c, c, 0};
  s.d.custom.customData = Custom(DrawSwatch, look);
  Open(s);
  Close();
}

void DrawFill(float x, float y, float w, float h, void *pCtx)
{
  const ScDraw &d = *static_cast<const ScDraw *>(pCtx);
  UiFill(x, y, w, h, d.look.rim, d.f);
}

// A bar of a part: its track, its fill to f of the width, and the other run's mark at n/1000.
void DrawPartBar(float x, float y, float w, float h, void *pCtx)
{
  const ScDraw &d = *static_cast<const ScDraw *>(pCtx);
  UiFill(x, y, w, h, kTrack, Sc(2));
  UiFill(x, y, w * fminf(fmaxf(d.f, 0.0f), 1.0f), h, d.look.rim, Sc(2));
  if (d.n >= 0) {
    const float m = x + w * fminf(d.n / 1000.0f, 1.0f);
    UiFill(m - Sc(1), y - Sc(3), Sc(2), h + Sc(6), kBmVs);
  }
}

void Checkbox(const char *pszId, const char *pszLabel, bool *pbOn, bool bLocked)
{
  if (ScCheck(Box(pszId), pszLabel, 0, *pbOn, !bLocked))
    *pbOn = !*pbOn;
}

// The options' rows, in SC2's units.
const float kOptionHeight = 46;

// ---------------------------------------------------------------------------
// New Run.

void ScenariosBox(float x, float y, float w)
{
  Open(InBox("bmscen", x, y, w, 226));
  BoxTitle(PortText("Scenarios"));
  Decl row;
  row.d.layout.sizing.width = Grow();
  row.d.layout.sizing.height = Fixed(Sc(150));
  row.d.layout.padding = Pad(0, 0, Fs(14), 0);
  row.d.layout.childGap = Gap(Fs(10));
  Open(row);
  const int nPressed = ScenarioPressed();
  for (int i = 0; i < kScenarioCount; ++i) {
    Decl c = Box("bmcard", i);
    c.d.layout.layoutDirection = CLAY_TOP_TO_BOTTOM;
    c.d.layout.sizing.width = Grow();
    c.d.layout.sizing.height = Grow();
    c.d.layout.padding = Pad(Fs(14), Fs(14), Fs(12), Fs(12));
    c.d.layout.childGap = Gap(Fs(4));
    const bool bOn = i == nPressed;
    const bool bHot = Over(c.d.id);
    c.d.backgroundColor = Col(bHot ? kLbItemHi : kLbItem);
    c.d.border = Border(bOn ? kGoldRim : kLbItemRim, Fs(2), Fs(2), Fs(2), Fs(2));
    Radius(c, 4);
    if (Clicked(c.d.id)) {
      const Scenario &s = kScenarios[i];
      g_s.nMap = s.nMap;
      g_s.nPlayers = s.nPlayers;
      g_s.nSpeed = s.nSpeed;
      g_s.nLength = s.nLength;
      FitPlayers();
    }
    Open(c);
    const Scenario &s = kScenarios[i];
    Text(PortText(s.name), UI_FACE_BOLD, Fs(19), 0.0f, bOn ? kBmVs : kLbInkInfo);
    Text(Keep(std::string(kMaps[s.nMap].shortName) + kDot +
              PortTextNF("{0} player", "{0} players", s.nPlayers, {std::to_string(s.nPlayers)})),
         UI_FACE_TEXT, Fs(15), 0.0f, kInkPlayer);
    Text(Keep(std::string(s.nSpeed == 7 ? PortText("Max speed") : PortText(kSpeeds[s.nSpeed])) + kDot +
              PortText(kLengths[s.nLength].label)),
         UI_FACE_TEXT, Fs(15), 0.0f, kInkPlayer);
    Decl sp;
    sp.d.layout.sizing.height = Grow();
    Open(sp);
    Close();
    Text(PortText(s.foot), UI_FACE_TEXT, Fs(13), 0.0f, kLbInkDesc);
    Close();
    if (s.nPlayers == 64)
      TipOn(c.d.id, PortText("Extreme"),
            PortText("Sixty-four computers on Big Game Hunters 8x8, 1,024 tiles a side, as fast as it goes for 30 minutes: "
                     "past the 32 players and 512 tiles Hyperion aims at for now, the most it holds. Its map is in "
                     "maps\\Hyperion\\Experimental\\."));
  }
  Close();
  Close();
}

// A slot of the players, as the skirmish draws its rows (lobby.js slotRow): its number, a
// computer, its colour, its race, the handicap, level and personality still to come, and in
// Team Match its team.
void SeatRow(int i)
{
  Open(SlotGround("bmseat", i, true, true));
  SlotNumber("bmnum", i, true);
  const std::string n = std::to_string(i + 1);
  // {0} the slot's number
  TipOn(Id("bmnum", i), Keep(PortTextF("Player {0}", {n})),
        g_s.nStartsBy == 1
            ? Keep(PortTextF("Starts by slot: player {0} starts where the map\xe2\x80\x99s picture shows {0}.", {n}))
            : PortText("The seed draws the starts, so a slot is not a place on the map. Choose Start positions: "
                       "By slot to give each player a start."));
  Decl who;
  who.d.layout.sizing.width = Grow();
  Open(who);
  Text(PortText("Computer"), UI_FACE_TEXT, Fs(17), 0.0f, kScInkLabel);
  Close();

  Decl tiles;
  tiles.d.layout.childGap = Gap(Fs(5));
  tiles.d.layout.childAlignment.y = CLAY_ALIGN_Y_CENTER;
  Open(tiles);
  SlotShade("bmshade", i, SeatColour(i), ColoursByTeam() ? Keep(g_s.teamNames[g_s.teams[i]]) : PortText("Its own"));
  TipOn(Id("bmshade", i), ColoursByTeam() ? PortText("Colours by team") : PortText("Colour"),
        ColoursByTeam()
            ? PortText("Each team plays in shades of its colour. Choose Colours: By player for each slot\xe2\x80\x99s own.")
            : PortText("Each slot plays in the game\xe2\x80\x99s colour for it. In Team Match, Colours: By team gives "
                       "each team shades of one colour."));
  std::vector<std::string> races;
  for (int k = 0; k < 4; ++k)
    races.push_back(PortText(kRaceNames[k]));
  int nRace = 3;
  for (int k = 0; k < 4; ++k)
    if (kRaceLetters[k] == SeatLetter(i))
      nRace = k;
  const int nPick = Pulldown("bmrace", i, 130, races, nRace);
  if (nPick >= 0)
    g_s.seats[i] = kRaceLetters[nPick];
  TipOn(Id("bmrace", i), PortText("Race"),
        PortText("Random is drawn from the seed: the same seed gives the same races."));
  PulldownLater("bmhcap", i, 92, "100%", PortText("Handicap"),
                PortText("A player\xe2\x80\x99s units at a share of their life, as in SC2. Not available yet."));
  const int nLevelNow = AiLevelFromLetter(g_s.levels[i]);
  const int nLevel = Pulldown("bmlevel", i, 200, LevelNames(), nLevelNow < 0 ? AI_LEVEL_CLASSIC : nLevelNow, PD_ON,
                              NULL, NULL, 190);
  if (nLevel >= 0)
    g_s.levels[i] = AiLevelLetter(nLevel);
  TipOn(Id("bmlevel", i), PortText("Computer level"), PortText(kLevelTip));
  PulldownLater("bmpers", i, 150, PortText("Its own script"), PortText("Personality"),
                PortText("How the computer plays: turtle, rusher, expander and the like. Not available yet."));
  if (TeamMatch()) {
    // the slot's team (lobby.js slotRow's t-team): one of the teams, or a new one
    std::vector<std::string> teams;
    std::vector<unsigned> chips;
    TeamChoices(g_s.teamNames, &teams, &chips);
    const int nTeam = Pulldown("bmteam", i, 140, teams, g_s.teams[i], PD_ON, &chips[0]);
    if (nTeam >= 0 && nTeam != g_s.teams[i])
      JoinTeam(i, nTeam);
  }
  Close();
  Close();
}

// The bar over the slots (the skirmish's tools): every computer's race, level and personality
// at once, and in Team Match New Team and the quick layouts.
void PlayersBar(void)
{
  Decl bar;
  bar.d.layout.sizing.width = Grow();
  bar.d.layout.padding = Pad(0, 0, Fs(8), Fs(10));
  bar.d.layout.childGap = Gap(Fs(8));
  bar.d.layout.childAlignment.y = CLAY_ALIGN_Y_CENTER;
  bar.d.border = Border(kLbDivider, 0, 0, 0, Fs(2));
  Open(bar);
  Label(PortText("All computers:"), 15, kLbInkDesc);
  // every slot's race: the four, or the three in turn
  const std::vector<std::string> names = {PortText(kRaceNames[0]), PortText(kRaceNames[1]), PortText(kRaceNames[2]),
                                          PortText(kRaceNames[3]), PortText("Terran, Zerg, Protoss in turn")};
  int nCur = -1;
  bool bTurn = g_s.nPlayers > 2;
  for (int i = 0; i < g_s.nPlayers; ++i)
    bTurn = bTurn && SeatLetter(i) == "tzp"[i % 3];
  for (int k = 0; k < 4; ++k)
    if (OneRace() && SeatLetter(0) == kRaceLetters[k])
      nCur = k;
  if (bTurn)
    nCur = 4;
  const int nAll = Pulldown("bmallrace", 0, 130, names, nCur, PD_ON, NULL, nCur < 0 ? PortText("Race") : NULL, 300);
  if (nAll >= 0)
    for (int i = 0; i < kSeats; ++i)
      g_s.seats[i] = nAll < 4 ? kRaceLetters[nAll] : "tzp"[i % 3];
  TipOn(Id("bmallrace", 0), PortText("Every computer\xe2\x80\x99s race"),
        PortText("Sets every slot\xe2\x80\x99s race at once; each slot\xe2\x80\x99s own list changes it after. Random is drawn "
                 "from the seed."));
  // every slot's level at once
  int nSameLevel = -2;
  for (int i = 0; i < g_s.nPlayers; ++i) {
    const int n = AiLevelFromLetter(g_s.levels[i]);
    nSameLevel = nSameLevel == -2 ? n : nSameLevel == n ? nSameLevel : -1;
  }
  const int nAllLevel = Pulldown("bmalllevel", 0, 200, LevelNames(), nSameLevel < 0 ? -1 : nSameLevel, PD_ON, NULL,
                                 nSameLevel < 0 ? PortText("Level") : NULL, 190);
  if (nAllLevel >= 0)
    for (int i = 0; i < kSeats; ++i)
      g_s.levels[i] = AiLevelLetter(nAllLevel);
  TipOn(Id("bmalllevel", 0), PortText("Every computer\xe2\x80\x99s level"), PortText(kLevelTip));
  PulldownLater("bmallpers", 0, 150, PortText("Its own script"), PortText("Every computer\xe2\x80\x99s personality"),
                PortText("How the computers play: turtle, rusher, expander and the like. Not available yet."));
  if (TeamMatch()) {
    Decl gap;
    gap.d.layout.sizing.width = Fixed(Sc(12));
    Open(gap);
    Close();
    Decl nt = Box("bmnewteam");
    nt.d.layout.sizing.width = Fixed(Sc(140));
    nt.d.layout.sizing.height = Fixed(Sc(36));
    if (ScMini(nt, PortText("New Team"), 0, static_cast<int>(g_s.teamNames.size()) < g_s.nPlayers)) {
      TeamsAdd(&g_s.teamNames);
      g_s.layout.clear();
    }
    TipOn(Id("bmnewteam"), PortText("New team"),
          PortText("Adds a team; choose it in a slot\xe2\x80\x99s Team list to move that computer to it."));
    // Quick layout (teams.js presets): the teams from where the map's starts are, each
    // playing from its part; a slot's Team list changes them after
    const std::vector<TeamLayout> &ls = Layouts();
    std::vector<std::string> items;
    std::vector<int> which;
    QuickLayoutChoices(ls, &items, &which);
    const int nQuick =
        Pulldown("bmquick", 0, 190, items, -1, which.empty() ? PD_OFF : PD_ON, NULL, PortText("Quick layout"), 380);
    if (nQuick >= 0 && nQuick < static_cast<int>(which.size()))
      ApplyLayout(ls[which[nQuick]]);
    TipOn(Id("bmquick", 0), PortText("Quick layout"),
          which.empty()
              ? PortText("The map\xe2\x80\x99s starts could not be read, so no layout can be worked out from them. New "
                         "Team and each slot\xe2\x80\x99s Team list make the teams.")
              : PortText("Sets the teams from where the map\xe2\x80\x99s starts are: halves of the map, or groups of "
                         "nearby starts. Each computer starts in its team\xe2\x80\x99s part (Start positions: By "
                         "slot). Move any of them with its Team list after."));
  }
  Close();
}

// The slots, in a frame for the game type, or one a team in Team Match (lobby.js teamFrame),
// the wheel scrolling through them.
void PlayersList(void)
{
  Decl scroll = Box("bmseats");
  scroll.d.layout.layoutDirection = CLAY_TOP_TO_BOTTOM;
  scroll.d.layout.sizing.width = Grow();
  scroll.d.layout.sizing.height = Grow();
  scroll.d.layout.padding = Pad(0, 0, Fs(10), 0);
  scroll.d.layout.childGap = Gap(Fs(8));
  scroll.d.clip.vertical = true;
  Open(scroll);
  const int nSeats = g_s.nPlayers;
  const bool bTeams = TeamMatch();
  const int nFrames = bTeams ? static_cast<int>(g_s.teamNames.size()) : 1;
  std::vector<int> order;
  for (int q = 0; q < nFrames; ++q)
    for (int i = 0; i < nSeats; ++i)
      if (!bTeams || g_s.teams[i] == q)
        order.push_back(i);
  // The rows that fit from the list's place, each team reached adding its head (an empty
  // team's always), and the last place it can scroll to: both change as the heads in view do
  const float fRow = Sc(40), fHead = Sc(36 + 8 + 8);
  const int nOrder = static_cast<int>(order.size());
  auto teamOf = [&](int k) { return bTeams ? g_s.teams[order[k]] : 0; };
  float fRoom = g_fSeatRoom > 0.0f ? g_fSeatRoom : fHead + 8 * fRow;
  if (bTeams)
    for (int q = 0; q < nFrames; ++q)
      if (std::count(g_s.teams.begin(), g_s.teams.begin() + nSeats, q) == 0)
        fRoom -= fHead;
  auto fitFrom = [&](int nFrom) {
    float f = fRoom;
    int k = nFrom;
    for (; k < nOrder; ++k) {
      f -= fRow + (k == nFrom || teamOf(k) != teamOf(k - 1) ? fHead : 0.0f);
      if (f < 0.0f && k > nFrom)
        break;
    }
    return k;
  };
  int nMaxFirst = nOrder > 0 ? nOrder - 1 : 0;
  while (nMaxFirst > 0 && fitFrom(nMaxFirst - 1) >= nOrder)
    --nMaxFirst;
  g_nSeatFirst += WheelRows(Over(Id("bmseats")));
  g_nSeatFirst = g_nSeatFirst < 0 ? 0 : g_nSeatFirst > nMaxFirst ? nMaxFirst : g_nSeatFirst;
  const int nEnd = fitFrom(g_nSeatFirst);
  int nDrawn = 0;
  for (int q = 0; q < nFrames; ++q) {
    std::vector<int> rows;
    int nMembers = 0;
    for (int k = 0; k < static_cast<int>(order.size()); ++k) {
      if (bTeams && g_s.teams[order[k]] != q)
        continue;
      ++nMembers;
      if (k >= g_nSeatFirst && k < nEnd)
        rows.push_back(order[k]);
    }
    // a team none of whose slots is in view is left out, an empty one shown to be joined
    if (rows.empty() && nMembers > 0)
      continue;
    const bool bFirst = nDrawn++ == 0;
    Decl team = Box("bmteamframe", q);
    team.d.layout.layoutDirection = CLAY_TOP_TO_BOTTOM;
    team.d.layout.sizing.width = Grow();
    team.d.layout.padding = Pad(Fs(4), Fs(4), 0, Fs(8));
    InnerBox(team, 1);
    Open(team);
    Decl head;
    head.d.layout.sizing.width = Grow();
    head.d.layout.sizing.height = Fixed(Sc(36));
    head.d.layout.padding = Pad(Fs(8), Fs(8), 0, 0);
    head.d.layout.childGap = Gap(Fs(10));
    head.d.layout.childAlignment.y = CLAY_ALIGN_Y_CENTER;
    Open(head);
    if (bTeams) {
      Chip(kTeamBases[q % 16], 16);
      Text(Keep(Upper(g_s.teamNames[q].c_str())), UI_FACE_WIDE, Fs(15), 0.04f, kScInkHead);
      Text(Keep(nMembers == 0 ? std::string(PortText("empty: choose it in a slot\xe2\x80\x99s Team list"))
                              : PortTextNF("{0} computer", "{0} computers", nMembers, {std::to_string(nMembers)})),
           UI_FACE_TEXT, Fs(14), 0.0f, kLbInkDesc);
    } else {
      Text(g_s.nGameType == 1 ? PortText("MELEE") : PortText("FREE FOR ALL"), UI_FACE_WIDE, Fs(15), 0.04f, kScInkHead);
      Text(Keep(g_s.nGameType == 1
                    ? PortTextNF("{0} computer on one side, as the original seats them",
                                 "{0} computers on one side, as the original seats them", nSeats,
                                 {std::to_string(nSeats)})
                    : PortTextNF("{0} computer, for itself", "{0} computers, each for itself", nSeats,
                                 {std::to_string(nSeats)})),
           UI_FACE_TEXT, Fs(14), 0.0f, kLbInkDesc);
    }
    Spacer();
    if (nMaxFirst > 0 && bFirst)
      // {0} the first row shown, {1} the last, {2} how many there are
      Text(Keep(PortTextF("{0} to {1} of {2} shown, the wheel scrolls",
                          {std::to_string(g_nSeatFirst + 1), std::to_string(nEnd), std::to_string(nSeats)})),
           UI_FACE_TEXT, Fs(14), 0.0f, kScInkLabel);
    Close();
    for (int i : rows)
      SeatRow(i);
    Close();
  }
  Close();
  // the room for rows and heads, for the next frame: the list's height less its padding
  const Clay_ElementData sc = Clay_GetElementData(Id("bmseats"));
  if (sc.found)
    g_fSeatRoom = sc.boundingBox.height - Sc(10);
}

void PlayersBox(float x, float y, float w, float h)
{
  Decl box = InBox("bmplayers", x, y, w, h);
  Open(box);
  Decl head;
  head.d.layout.sizing.width = Grow();
  head.d.layout.childGap = Gap(Fs(16));
  head.d.layout.childAlignment.y = CLAY_ALIGN_Y_CENTER;
  Open(head);
  BoxTitle(PortText("Players"));
  // the game type: every computer for itself, 1.17's melee, which puts them on one side, or
  // Team Match
  const std::vector<std::string> modes = {PortText("Free For All"), PortText("Melee, all allied"),
                                          PortText("Team Match")};
  const int nMode = Pulldown("bmmode", 0, 220, modes, g_s.nGameType);
  if (nMode >= 0 && nMode != g_s.nGameType) {
    g_s.nGameType = nMode;
    if (nMode == 2) {
      g_s.nColourBy = 1;
      g_s.nTeamsFor = -1;
      SyncTeams();
    }
  }
  TipOn(Id("bmmode", 0), PortText("Game type"),
        PortText("Free For All: every computer for itself. Melee: the original\xe2\x80\x99s, where every computer is on one "
                 "side and nothing fights, kept to compare with it. Team Match: the computers on teams, allied, sharing "
                 "vision and winning together; Quick layout sets the teams from the map\xe2\x80\x99s starts. Team Match "
                 "plays by Hyperion\xe2\x80\x99s rules. Runs are the same game only with the same teams and starts."));
  if (TeamMatch())
    Text(Keep(TeamsLine()), UI_FACE_TEXT, Fs(15), 0.0f, kInkPlayer);
  Spacer();
  const std::string you = ProfileCurrent();
  Text(Keep((you.empty() ? std::string(PortText("You")) : you) + kDot + PortText("observer") + kDot +
            PortText("sees the whole map, cannot lose")),
       UI_FACE_TEXT, Fs(15), 0.0f, kBmVs);
  Close();
  PlayersBar();
  PlayersList();
  Close();
}

void OptionsBox(float x, float y, float h)
{
  Decl box = At("bmopts", x, y, 438, h);
  box.d.layout.layoutDirection = CLAY_TOP_TO_BOTTOM;
  box.d.layout.padding = Pad(Fs(20), Fs(15), Fs(15), Fs(15));
  box.d.layout.childGap = Gap(Fs(4));
  box.d.clip.vertical = true;
  InnerBox(box);
  Open(box);
  // the tab over it names the box (NewRun)
  // Map
  {
    const MapRead &m = MapOf(g_s.nMap);
    Open(OptionRow(kOptionHeight));
    // {0} the map's width, {1} its height
    OptionName(PortText("Map"), m.bFound ? PortTextF("{0} \xc3\x97 {1} tiles",
                                                     {std::to_string(m.pic.w), std::to_string(m.pic.h)})
                                         : std::string(PortText("not in maps\\Hyperion\\")));
    std::vector<std::string> items;
    for (const MapDef &d : kMaps)
      items.push_back(d.shortName);
    const int n = Pulldown("bmmap", 0, 214, items, g_s.nMap);
    if (n >= 0) {
      g_s.nMap = n;
      FitPlayers();
    }
    Close();
  }
  // Players
  {
    Open(OptionRow(kOptionHeight));
    OptionName(PortText("Players"), PortText("all computers"));
    static const char *const kCounts[] = {"8", "16", "32", "64"};
    const int nStarts = MapStarts(g_s.nMap);
    const unsigned nOff =
        (nStarts < 8 ? 1u : 0u) | (nStarts < 16 ? 2u : 0u) | (nStarts < 32 ? 4u : 0u) | (nStarts < 64 ? 8u : 0u);
    const int nCur = g_s.nPlayers == 8 ? 0 : g_s.nPlayers == 16 ? 1 : g_s.nPlayers == 32 ? 2 : 3;
    // {0} the map's name, {1} how many starts it has
    const std::string why =
        PortTextNF("{0} has {1} start.", "{0} has {1} starts.", nStarts, {kMaps[g_s.nMap].title, std::to_string(nStarts)});
    const int n = Segments("bmcount", kCounts, 4, nCur, nOff, Keep(why), 36);
    if (n >= 0 && !(nOff >> n & 1))
      g_s.nPlayers = n == 0 ? 8 : n == 1 ? 16 : n == 2 ? 32 : 64;
    Close();
  }
  // Start positions
  {
    Open(OptionRow(kOptionHeight));
    OptionName(PortText("Start positions"),
               g_s.nStartsBy == 1 ? PortText("each at its number") : PortText("drawn from the seed"));
    const std::vector<std::string> items = {PortText("Random"), PortText("By slot")};
    const int n = Pulldown("bmstarts", 0, 214, items, g_s.nStartsBy, StartsKnown() ? PD_ON : PD_OFF);
    if (n >= 0)
      g_s.nStartsBy = n;
    TipOn(Id("bmstarts", 0), PortText("Start positions"),
          StartsKnown()
              ? PortText("Random: the seed draws each computer a start. By slot: each starts where the map\xe2\x80\x99s "
                         "picture shows its number. Either plays by the original\xe2\x80\x99s rules.")
              : PortText("The map\xe2\x80\x99s starts could not be read, so the seed draws them."));
    Close();
  }
  // Colours
  {
    Open(OptionRow(kOptionHeight));
    OptionName(PortText("Colours"), "");
    const std::vector<std::string> items = {PortText("By player"), PortText("By team")};
    const int n = Pulldown("bmcolourby", 0, 214, items, TeamMatch() ? g_s.nColourBy : 0, TeamMatch() ? PD_ON : PD_OFF);
    if (n >= 0)
      g_s.nColourBy = n;
    TipOn(Id("bmcolourby", 0), PortText("Colours"),
          TeamMatch() ? PortText("By player: each slot in the game\xe2\x80\x99s colour for it. By team: each team in shades of "
                                 "one colour, so a team reads at a glance.")
                      : PortText("Colours by team come with Team Match."));
    Close();
  }
  // Game speed
  {
    Open(OptionRow(kOptionHeight));
    // {0} how long a game frame is
    OptionName(PortText("Game speed"),
               g_s.nSpeed < kPortSpeeds
                   ? PortTextF("{0} ms a game frame", {std::to_string(PortSpeedFrameMs(g_s.nSpeed))})
                   : std::string(PortText("no wait between frames")));
    std::vector<std::string> items;
    for (int i = 0; i < kSpeedCount; ++i)
      items.push_back(PortText(kSpeeds[i]));
    const int n = Pulldown("bmspeed", 0, 214, items, g_s.nSpeed);
    if (n >= 0)
      g_s.nSpeed = n;
    Close();
  }
  // Length: the most a run goes, a preset or hours and minutes of its own
  {
    const bool bCustom = g_s.nLength >= kLengthCount;
    Open(OptionRow(kOptionHeight));
    // {0} how many game frames that is
    OptionName(PortText("Length"), bCustom ? std::string(PortText("at most"))
                                           : PortTextF("at most {0} frames", {Commas(LengthFrames())}));
    std::vector<std::string> items;
    for (const Length &l : kLengths)
      items.push_back(PortText(l.label));
    items.push_back(PortText("Custom"));
    const int n = Pulldown("bmlength", 0, bCustom ? 110 : 214, items, g_s.nLength, PD_ON, NULL, NULL, 214);
    if (n >= 0 && n != g_s.nLength) {
      if (n >= kLengthCount && g_s.nLength < kLengthCount) {
        // a custom length starts from the preset it leaves, in whole minutes
        g_s.nCustomMinutes = static_cast<int>(lround(LengthFrames() * 42.0 / 60000.0));
        g_s.nCustomMinutes = g_s.nCustomMinutes < 1 ? 1 : g_s.nCustomMinutes;
        LengthFields();
        g_nEdit = 1;
      }
      g_s.nLength = n;
    }
    // {0} how many game frames the length chosen is
    TipOn(Id("bmlength", 0), PortText("Length"),
          Keep(PortTextF("The longest the run goes: it ends at this length, or sooner when one player or team is left. "
                         "A minute is a minute of the game at Fastest, 42 ms a game frame; this is {0} game frames. "
                         "Custom takes hours and minutes, up to 24 hours.",
                         {Commas(LengthFrames())})));
    if (bCustom) {
      // hours and minutes, typed; a click puts the keyboard in one, Tab moves it, Enter ends
      const char *const apszIds[2] = {"bmhours", "bmminutes"};
      // the units after each field, short: hours and minutes
      const char *const apszUnits[2] = {PTC_("length", "h"), PTC_("length", "min")};
      std::string *apText[2] = {&g_hours, &g_minutes};
      for (int k = 0; k < 2; ++k) {
        Decl e = Box(apszIds[k]);
        e.d.layout.sizing.width = Fixed(Sc(52));
        e.d.layout.sizing.height = Fixed(Sc(36));
        if (Clicked(e.d.id))
          g_nEdit = k + 1;
        // the first key typed in a field just given the keyboard replaces what it held
        if (g_nEdit != g_nEditWas) {
          g_nEditWas = g_nEdit;
          g_bFresh = true;
        }
        if (g_nEdit == k + 1 && g_bFresh && (!g_typed.empty() || Key(VK_BACK))) {
          apText[k]->clear();
          g_bFresh = false;
        }
        ScEdit(e, apText[k], 2, "0", g_nEdit == k + 1);
        std::string digits;
        for (char c : *apText[k])
          if (c >= '0' && c <= '9')
            digits += c;
        *apText[k] = digits;
        Label(PortTextC("length", apszUnits[k]), 15, kLbInkDesc);
      }
      const int nHours = (std::min)(atoi(g_hours.c_str()), 24);
      const int nMinutes = (std::min)(atoi(g_minutes.c_str()), 59);
      const int nAll = nHours * 60 + nMinutes;
      g_s.nCustomMinutes = nAll < 1 ? 1 : nAll > 24 * 60 ? 24 * 60 : nAll;
      // a click elsewhere takes the keyboard back, and the fields show what counts
      if (g_bReleased && !Over(Id("bmhours")) && !Over(Id("bmminutes")) && g_nEdit != 0) {
        g_nEdit = 0;
        LengthFields();
      }
    }
    Close();
  }
  // Seed
  {
    Open(OptionRow(kOptionHeight));
    OptionName(PortText("Seed"), PortText("the same game each run"));
    Decl seed;
    seed.d.layout.sizing.width = Fixed(Sc(214));
    seed.d.layout.childGap = Gap(Fs(10));
    seed.d.layout.childAlignment.x = CLAY_ALIGN_X_RIGHT;
    seed.d.layout.childAlignment.y = CLAY_ALIGN_Y_CENTER;
    Open(seed);
    Text(Keep(std::to_string(g_s.nSeed)), UI_FACE_TEXT, Fs(16), 0.04f, kLbInkInfo);
    Decl b = Box("bmnewseed");
    b.d.layout.sizing.height = Fixed(Sc(36));
    if (ScMini(b, PortText("New"), 'N'))
      g_s.nSeed = 10000000u + static_cast<unsigned>(SDL_rand(90000000));
    TipOn(b.d.id, PortText("A new seed"),
          PortText("Another game. Runs compare only on the same seed: the same seed, map and players is the same game, frame "
                   "for frame, in any build that plays the same rules."));
    Close();
    Close();
  }
  // Supply cap and Unit limit: the mutators a large battle needs
  {
    Open(OptionRow(kOptionHeight));
    OptionName(PortText("Supply cap"),
               g_s.mut.nSupplyCap == 200 ? PortText("the original\xe2\x80\x99s") : PortText("each player"));
    static const int kCaps[] = {200, 300, 400, 500, 600, 800, 1000, 0};
    std::vector<std::string> items;
    int nCur = -1;
    for (int i = 0; i < 8; ++i) {
      items.push_back(kCaps[i] == 0 ? std::string(PortText("Unlimited")) : Commas(kCaps[i]));
      if (kCaps[i] == g_s.mut.nSupplyCap)
        nCur = i;
    }
    // a cap the Mutators tab set that the list has not, 100, shows as itself
    if (nCur < 0) {
      nCur = 8;
      items.push_back(Commas(g_s.mut.nSupplyCap));
    }
    const int n = Pulldown("bmsupply", 0, 214, items, nCur);
    if (n >= 0 && n < 8)
      g_s.mut.nSupplyCap = kCaps[n];
    TipOn(Id("bmsupply", 0), PortText("Supply cap"),
          PortText("The most supply each computer holds. The original\xe2\x80\x99s is 200; a higher cap plays by "
                   "Hyperion\xe2\x80\x99s rules, and with the unit limit at Automatic the game makes room for every "
                   "player\xe2\x80\x99s whole cap in units."));
    Close();
  }
  {
    Open(OptionRow(kOptionHeight));
    OptionName(PortText("Unit limit"),
               g_s.mut.nUnits == 0 ? PortText("room for the supply cap") : PortText("units at once, at least"));
    std::vector<std::string> items;
    for (int i = 0; i < MutatorUnitLimits(); ++i)
      items.push_back(MutatorUnitLimitName(i));
    const int n = Pulldown("bmunits", 0, 214, items, MutatorUnitLimitOf(g_s.mut.nUnits));
    if (n >= 0)
      g_s.mut.nUnits = MutatorUnitLimit(n);
    TipOn(Id("bmunits", 0), PortText("Unit limit"),
          PortText("The most units the game holds at once, every player\xe2\x80\x99s and the map\xe2\x80\x99s minerals "
                   "and geysers together. The original holds 1,700. Automatic: the original\xe2\x80\x99s in a game it "
                   "could play, and with more players or a supply cap past 200, room for the map\xe2\x80\x99s own and for "
                   "each player\xe2\x80\x99s whole cap in units. A number plays by Hyperion\xe2\x80\x99s rules."));
    Close();
  }
  // Renderer, Shown, Frames shown
  {
    Open(OptionRow(kOptionHeight));
    OptionName(PortText("Renderer"), "");
    const std::vector<std::string> items = {"GPU", PortText("Classic 256 colours")};
    const int n = Pulldown("bmrender", 0, 214, items, g_s.nPicture);
    if (n >= 0)
      g_s.nPicture = n;
    Close();
  }
  {
    // the screen: the game in a window or full screen and the overlay with it; O changes the
    // overlay during the run, and the Overlay button sets its corner, size and see-through
    Open(OptionRow(kOptionHeight));
    const char *const apszWhat[PORT_BENCH_STYLES] = {
        PT_("panels over the game"),   PT_("large figures in a corner"), PT_("the game alone"),
        PT_("figures in a corner"),    PT_("figures along the foot"),    PT_("a 1280 \xc3\x97 720 window"),
        PT_("numbers only")};
    const int nStyle = PortBenchStyleOf(g_s.nShown, g_s.overlay);
    OptionName(PortText("Screen"), nStyle >= 0 ? PortText(apszWhat[nStyle]) : PortText("from the command line"));
    std::vector<std::string> items;
    for (int i = 0; i < PORT_BENCH_STYLES; ++i)
      items.push_back(PortBenchStyleName(i));
    if (nStyle < 0)
      items.push_back(PortBenchScreenName(g_s.nShown, g_s.overlay));
    const int n = Pulldown("bmshown", 0, 214, items, nStyle >= 0 ? nStyle : PORT_BENCH_STYLES);
    if (n >= 0 && n < PORT_BENCH_STYLES)
      PortBenchStyleApply(n, &g_s.nShown, &g_s.overlay);
    Close();
  }
  {
    Open(OptionRow(kOptionHeight));
    OptionName(PortText("Frames shown"), g_s.nShown == 2 ? PortText("none, with no picture")
                                         : g_s.nSpeed == 7 && g_s.nFramesShown == 0
                                             ? PortText("up to 60 at this speed")
                                             : "");
    const SDL_DisplayMode *pMode = SDL_GetDesktopDisplayMode(SDL_GetPrimaryDisplay());
    const int nHz = pMode != NULL && pMode->refresh_rate > 20.0f ? static_cast<int>(pMode->refresh_rate + 0.5f) : 60;
    // {0} the display's frames a second
    const std::vector<std::string> items = {PortText("As many as it can"),
                                            PortTextF("Held to the display, {0}", {std::to_string(nHz)})};
    const int n = Pulldown("bmframes", 0, 214, items, g_s.nFramesShown, g_s.nShown == 2 ? PD_OFF : 0);
    if (n >= 0)
      g_s.nFramesShown = n;
    Close();
  }
  Close();
}

void LogBox(float x, float y, float w, float h)
{
  Decl box = InBox("bmlog", x, y, w, h);
  box.d.layout.childGap = Gap(Fs(2));
  Open(box);
  BoxTitle(PortText("Log"));
  Text(PortText("settings.json, seconds.csv and summary.json, always"), UI_FACE_TEXT, Fs(15), 0.0f, kLbInkDesc);
  Checkbox("bmlogframes", PortText("Every shown frame\xe2\x80\x99s time"), &g_s.bFramesCsv, false);
  bool bReplay = true;
  Checkbox("bmlogreplay", PortText("The replay, to run the same game again"), &bReplay, true);
  TipOn(Id("bmlogreplay"), PortText("The replay"), PortText("A run plays from its replay, so every run keeps it."));
  Text(Keep("benchmarks\\" + Stamp() + " " + kMaps[g_s.nMap].title + ", " + std::to_string(g_s.nPlayers) +
            " players\\"),
       UI_FACE_TEXT, Fs(13), 0.0f, kInkPlayer);
  Close();
}

void InfoBox(float x, float y, float h)
{
  const MapRead &m = MapOf(g_s.nMap);
  ShowMap(g_s.nMap);
  Decl box = At("bminfo", x, y, 425, h);
  box.d.layout.layoutDirection = CLAY_TOP_TO_BOTTOM;
  box.d.layout.padding = Pad(Fs(11), Fs(11), Fs(10), Fs(10));
  box.d.layout.childGap = Gap(Fs(6));
  InnerBox(box);
  Open(box);
  const float fSide = fminf(399.0f, h - 191.0f);
  // starts by slot: each player's number at its start, in its colour in Team Match
  static std::vector<MapMark> s_marks;
  const bool bMarks = g_s.nStartsBy == 1 && StartsKnown();
  if (bMarks) {
    s_marks.assign(m.pic.starts.size(), MapMark());
    for (int i = 0; i < g_s.nPlayers; ++i) {
      s_marks[i].nNum = i + 1;
      if (TeamMatch()) {
        s_marks[i].bRgb = true;
        s_marks[i].rgb = ColoursByTeam() ? SeatColour(i) : kTeamBases[g_s.teams[i] % 16];
      }
    }
  }
  MapPreview("bmmappic", m.bFound ? &m.pic : NULL, m.bFound ? m.pic.tileset : -1, m.bFound ? kMaps[g_s.nMap].title : NULL,
             m.pic.w, m.pic.h, fSide, fSide, 30, false, false, bMarks ? &s_marks : NULL);
  Text(kMaps[g_s.nMap].title, UI_FACE_BOLD, Fs(24), 0.0f, kLbInkInfo);
  if (m.bFound) {
    // {0} the width, {1} the height, {2} how many starts, {3} how many are seated, {4} the tileset
    Text(Keep(PortTextF("{0} \xc3\x97 {1} tiles", {std::to_string(m.pic.w), std::to_string(m.pic.h)}) + kDot +
              PortTextF("{0} starts, {1} seated", {std::to_string(m.nStarts), std::to_string(g_s.nPlayers)}) + kDot +
              PortText(kTilesets[m.pic.tileset & 7])),
         UI_FACE_TEXT, Fs(16), 0.0f, kLbInkDesc);
    // {0} what the map is: so many copies of Big Game Hunters
    Text(Keep(PortTextF("{0}, each mirrored against its neighbours.", {PortText(kMaps[g_s.nMap].line)})), UI_FACE_TEXT,
         Fs(16), 0.0f, kLbInkDesc, -1, 1.25f);
    Text(Keep(std::string(kMaps[g_s.nMap].title) + ".hxm" + kDot + PortText("Hyperion map")), UI_FACE_TEXT, Fs(14),
         0.0f, kScInkOff);
  } else {
    // {0} the map's name
    Text(Keep(PortTextF("maps\\Hyperion\\{0}.hxm is not in the game\xe2\x80\x99s folder or the data folder.",
                        {kMaps[g_s.nMap].title})),
         UI_FACE_TEXT, Fs(16), 0.0f, kLbWarn, -1, 1.25f);
  }
  Close();
}

void CommandStrip(float x, float y, float w)
{
  Decl strip = At("bmcmd", x, y, w, 52);
  strip.d.layout.childGap = Gap(Fs(12));
  strip.d.layout.childAlignment.y = CLAY_ALIGN_Y_CENTER;
  Open(strip);
  Text(PortText("COMMAND LINE"), UI_FACE_WIDE, Fs(13), 0.04f, kScInkHead);
  Decl code;
  code.d.layout.sizing.width = Grow();
  code.d.layout.padding = Pad(Fs(12), Fs(12), Fs(8), Fs(8));
  code.d.backgroundColor = Col(kLbInner);
  code.d.border = Border(kLbInnerRim, 1, 1, 1, 1);
  code.d.clip.horizontal = true;
  Radius(code, 3);
  Open(code);
  const std::string line = CommandLine();
  Text(Keep(line), UI_FACE_TEXT, Fs(14), 0.0f, kLbInkInfo);
  Close();
  Decl b = Box("bmcopy");
  b.d.layout.sizing.height = Fixed(Sc(38));
  if (ScMini(b, PortText("Copy"), 0)) {
    SDL_SetClipboardText(line.c_str());
    Note(PortText("Copied: the same run from a terminal, with no menus."));
  }
  Close();
}

// What the Mutators tab reads: the set-up's set, which every computer plays by, and the map.
MutatorsSetUp MutatorsOfSetUp(void)
{
  const MapRead &m = MapOf(g_s.nMap);
  MutatorsSetUp s;
  s.pSet = &g_s.mut;
  s.bMine = true;
  s.pMap = m.bFound ? &m.pic : NULL;
  s.pszMapTitle = kMaps[g_s.nMap].title;
  s.pszSpeed = kSpeeds[g_s.nSpeed < 0 ? 0 : g_s.nSpeed > 7 ? 7 : g_s.nSpeed];
  s.bMapUnits = false;
  return s;
}

void NewRun(float fW, float fH)
{
  const float info = 425, opts = 438, gap = 10, right = 34;
  const float infoX = fW - right - info;
  const float optsX = infoX - gap - opts;
  const float leftW = optsX - gap - 13;
  const float low = fH - 33 - 52 - 10;
  // the right columns under two tabs, as the skirmish set-up's (port_front_mutators.h): the
  // options the whole height beside the map and the log, or the Mutators tab across both.
  // Held Against, the run this one is held against, is set aside (the user, 2026-10-26); Runs
  // still names it.
  const float top = 93 + 46, logH = 206, colW = opts + gap + info;
  const float infoH = low - top - gap - logH;
  SyncTeams();
  ScenariosBox(13, 93, leftW);
  PlayersBox(13, 329, leftW, low - 329);
  if (g_nView == 1) {
    MutatorsTab(optsX, top, colW, low - top, MutatorsOfSetUp());
  } else {
    OptionsBox(optsX, top, low - top);
    InfoBox(infoX, top, infoH);
    LogBox(infoX, top + infoH + gap, info, logH);
  }
  // the tabs after the boxes, so they draw over the boxes' rims
  const int nView = MutatorsViewTabs(optsX, 93, colW, g_nView, g_s.mut, PortText("BENCHMARK OPTIONS"),
                                     PortText("The map, players, speed, length, supply cap and unit limit, and how the "
                                              "run is shown. Hotkey: O"));
  if (nView >= 0)
    g_nView = nView;
  Decl s = At("bmstart", 40, fH - 33 - 52, 340, 52);
  if (ScButton(s, PortText("Start Benchmark"), 'G', true))
    Start();
  Decl b = At("bmback", 392, fH - 33 - 52, 200, 52);
  if (ScButton(b, PortText("Back"), 'B', false))
    g_nResult = FRONT_BENCH_HOME;
  CommandStrip(616, fH - 33 - 52, fW - 616 - right);
}

// ---------------------------------------------------------------------------
// Runs.

// The text cut at a word to fit the width, with an ellipsis.
std::string Shorten(const std::string &s, float fSize, float fWidth)
{
  if (UiTextWidth(UI_FACE_TEXT, fSize, 0.0f, s.c_str()) <= fWidth)
    return s;
  std::string cut = s;
  while (!cut.empty()) {
    const size_t at = cut.find_last_of(' ');
    cut.resize(at == std::string::npos ? 0 : at);
    while (!cut.empty() && (cut.back() == ' ' || cut.back() == ',' || (cut.back() & 0x80) != 0))
      cut.pop_back();
    if (UiTextWidth(UI_FACE_TEXT, fSize, 0.0f, (cut + "\xe2\x80\xa6").c_str()) <= fWidth)
      return cut + "\xe2\x80\xa6";
  }
  return s;
}

const float kRunCols[] = {1.05f, 0.9f, 3.1f, 1.0f, 0.85f, 1.0f, 0.9f};

void RunsList(float x, float y, float w, float h)
{
  Decl box = At("bmruns", x, y, w, h);
  box.d.layout.layoutDirection = CLAY_TOP_TO_BOTTOM;
  box.d.clip.vertical = true;
  InnerBox(box);
  Open(box);
  float fSum = 0;
  for (float f : kRunCols)
    fSum += f;
  const float fInner = w - 24 - 6 * 10;
  auto colW = [&](int c) { return Sc(fInner * kRunCols[c] / fSum); };
  Decl head;
  head.d.layout.sizing.width = Grow();
  head.d.layout.sizing.height = Fixed(Sc(36));
  head.d.layout.padding = Pad(Fs(12), Fs(12), 0, 0);
  head.d.layout.childGap = Gap(Fs(10));
  head.d.layout.childAlignment.y = CLAY_ALIGN_Y_CENTER;
  head.d.backgroundColor = Col(kScMenuFill);
  Open(head);
  static const char *const kHeads[] = {PT_("When"),            PT_("Build"),         PT_("Scenario"),
                                       PT_("Frames a second"), PT_("Worst 1 %"),     PT_("Game frames/s"),
                                       PT_("Most units")};
  for (int c = 0; c < 7; ++c) {
    Decl hc;
    hc.d.layout.sizing.width = Fixed(colW(c));
    if (c >= 3)
      hc.d.layout.childAlignment.x = CLAY_ALIGN_X_RIGHT;
    Open(hc);
    Text(Keep(Upper(PortText(kHeads[c]))), UI_FACE_WIDE, Fs(12), 0.04f, kScInkHead);
    Close();
  }
  Close();
  // rows: the wheel scrolls, and as many as fit are shown
  const int nRows = static_cast<int>(g_runs.size());
  g_nShownRows = static_cast<int>((h - 36 - 8) / 52);
  if (g_nShownRows < 1)
    g_nShownRows = 1;
  g_nFirst += WheelRows(Over(Id("bmruns")));
  const int nMaxFirst = nRows > g_nShownRows ? nRows - g_nShownRows : 0;
  g_nFirst = g_nFirst < 0 ? 0 : g_nFirst > nMaxFirst ? nMaxFirst : g_nFirst;
  const int nAgainst = g_nAgainst == -2 ? Baseline(g_nPick) : g_nAgainst;
  if (g_runs.empty()) {
    Decl e;
    e.d.layout.padding = Pad(Fs(14), Fs(14), Fs(20), 0);
    Open(e);
    Text(PortText("No runs yet: a run\xe2\x80\x99s folder appears under benchmarks\\ in the data folder."),
         UI_FACE_TEXT, Fs(16), 0.0f, kLbInkDesc);
    Close();
  }
  for (int i = g_nFirst; i < nRows && i < g_nFirst + g_nShownRows; ++i) {
    const Run &r = g_runs[i];
    Decl row = Box("bmrun", i);
    row.d.layout.sizing.width = Grow();
    row.d.layout.sizing.height = Fixed(Sc(52));
    row.d.layout.padding = Pad(Fs(12), Fs(12), 0, 0);
    row.d.layout.childGap = Gap(Fs(10));
    row.d.layout.childAlignment.y = CLAY_ALIGN_Y_CENTER;
    row.d.border = Border(kLbDivider, 0, 0, 0, 1);
    const bool bPick = i == g_nPick;
    row.d.backgroundColor = Col(bPick ? kLbItem : Over(row.d.id) ? kLbItemHi : kNone);
    if (Clicked(row.d.id)) {
      g_nPick = i;
      g_nAgainst = -2;
    }
    Open(row);
    if (bPick || i == nAgainst) {
      Decl bar = Floating(NULL, 0, 0, 0, 25, CLAY_ATTACH_POINT_LEFT_TOP, CLAY_ATTACH_POINT_LEFT_TOP);
      bar.d.layout.sizing.width = Fixed(Sc(3));
      bar.d.layout.sizing.height = Fixed(Sc(52));
      bar.d.backgroundColor = Col(bPick ? kBmThis : kBmVs);
      Open(bar);
      Close();
    }
    const std::string cells[7] = {ShortWhen(r),
                                  r.commit.empty() ? std::string(kDash) : r.commit,
                                  RunScenario(r),
                                  !r.bSummary || r.shown == "none" ? std::string(kDash)
                                                                   : std::to_string(static_cast<int>(r.fps + 0.5)),
                                  !r.bSummary || r.shown == "none"
                                      ? std::string(kDash)
                                      : std::to_string(static_cast<int>(r.fpsWorst + 0.5)),
                                  !r.bSummary ? std::string(kDash) : Fixed1(r.gfps),
                                  !r.bSummary ? std::string(kDash) : std::to_string(r.most)};
    for (int c = 0; c < 7; ++c) {
      Decl cell;
      cell.d.layout.layoutDirection = CLAY_TOP_TO_BOTTOM;
      cell.d.layout.sizing.width = Fixed(colW(c));
      if (c >= 3)
        cell.d.layout.childAlignment.x = CLAY_ALIGN_X_RIGHT;
      Open(cell);
      // cut to the cell, as Clay clips only ten elements a frame
      Text(Keep(Shorten(cells[c], Fs(15), colW(c))), UI_FACE_TEXT, Fs(15), 0.0f, c == 0 ? kInkPlayer : kLbInkInfo);
      if (c == 1 && !r.branch.empty())
        Text(Keep(r.branch), UI_FACE_TEXT, Fs(12), 0.0f, kLbInkDesc);
      Close();
    }
    Close();
  }
  Close();
}

// The detail's graph: this run's frame time over the game, and the other's dashed.
struct GraphData {
  const Run *pThis = NULL;
  const Run *pOther = NULL;
};
GraphData g_graph;

void DrawRunGraph(float x, float y, float w, float h, void *)
{
  const float l = x + Sc(36), r = x + w - Sc(4), t = y + Sc(8), b = y + h - Sc(18);
  const Run *a = g_graph.pThis;
  const Run *o = g_graph.pOther;
  if (a == NULL || a->secMs.size() < 2) {
    const char *const pszNone = PortText("This run has no seconds to draw.");
    UiText(x + w / 2 - UiTextWidth(UI_FACE_TEXT, Sc(13), 0.0f, pszNone) / 2, y + h / 2, UI_FACE_TEXT, Sc(13), 0.0f,
           kLbInkDesc, pszNone);
    return;
  }
  float fTop = 0, fSpan = a->secFrame.back();
  std::vector<float> all(a->secMs);
  if (o != NULL && !o->secMs.empty()) {
    all.insert(all.end(), o->secMs.begin(), o->secMs.end());
    fSpan = fmaxf(fSpan, o->secFrame.back());
  }
  std::sort(all.begin(), all.end());
  const float v = all[all.size() * 98 / 100];
  static const float kNice[] = {2, 4, 5, 8, 10, 12, 16, 20, 25, 30, 40, 50, 80, 100};
  fTop = 150;
  for (float n : kNice)
    if (n >= v * 1.1f) {
      fTop = n;
      break;
    }
  fSpan = fmaxf(fSpan, static_cast<float>(a->gameFrames));  // the seconds' sum falls short of the last
  if (fSpan <= 0)
    fSpan = 1;
  const int nTicks = fTop <= 10 ? 2 : 4;
  for (int k = 0; k <= nTicks; ++k) {
    const float val = fTop * k / nTicks;
    const float yy = b - (b - t) * k / nTicks;
    UiFill(l, yy, r - l, fmaxf(1.0f, Sc(1)), kGrid);
    char sz[16];
    snprintf(sz, sizeof sz, "%g", val);
    UiText(x + Sc(30) - UiTextWidth(UI_FACE_TEXT, Sc(11), 0.0f, sz), yy + Sc(4), UI_FACE_TEXT, Sc(11), 0.0f,
           KitRgb(0x8dbfea, 0.85f), sz);
  }
  auto px = [&](float f) { return l + (r - l) * f / fSpan; };
  auto py = [&](float ms) { return b - (b - t) * fminf(ms / fTop, 1.0f); };
  if (o != NULL && o->secMs.size() >= 2)
    for (size_t i = 1; i < o->secMs.size(); ++i) {
      // dashed: every other piece
      if (i % 2 == 0)
        UiLine(px(o->secFrame[i - 1]), py(o->secMs[i - 1]), px(o->secFrame[i]), py(o->secMs[i]), kBmVs, Sc(1.2f));
    }
  for (size_t i = 1; i < a->secMs.size(); ++i) {
    UiFill(px(a->secFrame[i - 1]), py(a->secMs[i]), fmaxf(px(a->secFrame[i]) - px(a->secFrame[i - 1]), 1.0f),
           b - py(a->secMs[i]), kBand);
    UiLine(px(a->secFrame[i - 1]), py(a->secMs[i - 1]), px(a->secFrame[i]), py(a->secMs[i]), kBmThis, Sc(1.6f));
  }
  UiText(l, b + Sc(14), UI_FACE_TEXT, Sc(11), 0.0f, KitRgb(0x8dbfea, 0.85f), PortText("game 0:00"));
  const std::string end = Clock(fSpan / 23.81f);
  UiText(r - UiTextWidth(UI_FACE_TEXT, Sc(11), 0.0f, end.c_str()), b + Sc(14), UI_FACE_TEXT, Sc(11), 0.0f,
         KitRgb(0x8dbfea, 0.85f), end.c_str());
}

// A figure of the detail: its name, its value, and how it stands against the other run's.
void Figure(const char *pszName, const std::string &value, double dThis, double dOther, bool bHigherBetter,
            bool bNeutral)
{
  Decl f;
  f.d.layout.layoutDirection = CLAY_TOP_TO_BOTTOM;
  f.d.layout.sizing.width = Grow();
  f.d.layout.padding = Pad(Fs(12), Fs(12), Fs(10), Fs(10));
  f.d.layout.childGap = Gap(Fs(2));
  f.d.backgroundColor = Col(kLbItem);
  f.d.border = Border(kLbItemRim, 1, 1, 1, 1);
  Radius(f, 3);
  Open(f);
  Text(PortText(pszName), UI_FACE_TEXT, Fs(13), 0.0f, kLbInkDesc);
  Text(Keep(value), UI_FACE_BOLD, Fs(28), 0.0f, kInkWhite);
  if (dOther > 0.0) {
    const double d = (dThis - dOther) / dOther * 100.0;
    char sz[32];
    snprintf(sz, sizeof sz, "%+.1f %%", d);
    const bool bBetter = bHigherBetter ? d > 0.5 : d < -0.5;
    const bool bWorse = bHigherBetter ? d < -0.5 : d > 0.5;
    Text(Keep(sz), UI_FACE_TEXT, Fs(13), 0.0f, bNeutral ? kLbInkDesc : bBetter ? kBmGood : bWorse ? kBmBad : kLbInkDesc);
  } else {
    Decl none;
    none.d.layout.sizing.height = Fixed(Sc(17));
    Open(none);
    Close();
  }
  Close();
}

void PartBars(const Run &a, const Run *o)
{
  double dMost = 0.0;
  for (int i = 0; i < PERF_PARTS; ++i) {
    dMost = fmax(dMost, a.parts[i]);
    if (o != NULL)
      dMost = fmax(dMost, o->parts[i]);
  }
  if (dMost <= 0.0)
    dMost = 1.0;
  for (int i = 0; i < PERF_PARTS; ++i) {
    Decl row = Box("bmpart", i);
    row.d.layout.sizing.width = Grow();
    row.d.layout.sizing.height = Fixed(Sc(i == 8 ? 28 : 18));
    row.d.layout.childGap = Gap(Fs(8));
    row.d.layout.childAlignment.y = CLAY_ALIGN_Y_BOTTOM;
    Open(row);
    Decl name;
    name.d.layout.sizing.width = Fixed(Sc(78));
    Open(name);
    Text(PortBenchPartName(i), UI_FACE_TEXT, Fs(13), 0.0f, kLbInkDesc);
    Close();
    Decl bar;
    bar.d.layout.sizing.width = Grow();
    bar.d.layout.sizing.height = Fixed(Sc(10));
    bar.d.custom.customData = Custom(DrawPartBar, ScLook{kPartColours[i], kPartColours[i], kPartColours[i],
                                                         kPartColours[i], kPartColours[i], 0},
                                     static_cast<float>(a.parts[i] / dMost),
                                     o != NULL ? static_cast<int>(o->parts[i] / dMost * 1000.0) : -1);
    Open(bar);
    Close();
    Decl val;
    val.d.layout.sizing.width = Fixed(Sc(52));
    val.d.layout.childAlignment.x = CLAY_ALIGN_X_RIGHT;
    Open(val);
    char sz[32];
    snprintf(sz, sizeof sz, "%.2f", a.parts[i]);
    Text(Keep(sz), UI_FACE_TEXT, Fs(13), 0.0f, kLbInkInfo);
    Close();
    Decl dl;
    dl.d.layout.sizing.width = Fixed(Sc(50));
    dl.d.layout.childAlignment.x = CLAY_ALIGN_X_RIGHT;
    Open(dl);
    if (o != NULL && o->parts[i] > 0.0 && fmax(a.parts[i], o->parts[i]) >= 0.01) {
      const double d = (a.parts[i] - o->parts[i]) / o->parts[i] * 100.0;
      snprintf(sz, sizeof sz, "%+.0f %%", d);
      Text(Keep(sz), UI_FACE_TEXT, Fs(13), 0.0f, d < -0.5 ? kBmGood : d > 0.5 ? kBmBad : kLbInkDesc);
    }
    Close();
    Close();
    // {0} what the part is
    TipOn(Id("bmpart", i), PortBenchPartName(i),
          Keep(i < 8 ? PortTextF("{0}; part of the game frame", {PortText(kPartNotes[i])})
                     : PortTextF("{0}; part of the picture", {PortText(kPartNotes[i])})));
  }
  Text(o != NULL ? PortText("ms a game frame, averaged over the run; the mark is the other run")
                 : PortText("ms a game frame, averaged over the run"),
       UI_FACE_TEXT, Fs(12), 0.0f, kScInkOff);
}

void RunDetail(float x, float y, float w, float h)
{
  Decl box = InBox("bmdetail", x, y, w, h);
  box.d.layout.childGap = Gap(Fs(8));
  box.d.clip.vertical = true;
  Open(box);
  if (g_nPick < 0 || g_nPick >= static_cast<int>(g_runs.size())) {
    BoxTitle(PortText("No run picked"));
    Close();
    return;
  }
  Run &a = g_runs[g_nPick];
  const int nOther = g_nAgainst == -2 ? Baseline(g_nPick) : g_nAgainst;
  Run *o = nOther >= 0 && nOther < static_cast<int>(g_runs.size()) && nOther != g_nPick ? &g_runs[nOther] : NULL;
  ReadSeconds(a);
  if (o != NULL)
    ReadSeconds(*o);
  // {0} the map, {1} how many players
  BoxTitle(Keep(PortTextNF("{0}, {1} player", "{0}, {1} players", a.players, {a.title, std::to_string(a.players)})));
  // {0} the build, {1} its branch
  std::string line = a.when + kDot +
                     (a.branch.empty() ? PortTextF("build {0}", {a.commit.empty() ? std::string("?") : a.commit})
                                       : PortTextF("build {0} ({1})",
                                                   {a.commit.empty() ? std::string("?") : a.commit, a.branch})) +
                     kDot + PortText(kSpeeds[SpeedIndex(a.speed)]) + kDot + LengthLabel(a.frames) + kDot +
                     PortTextF("seed {0}", {std::to_string(a.seed)}) + kDot;
  // {0} where in the game it stopped
  line += a.ended == "stopped" ? PortTextF("stopped at game {0}", {Clock(a.gameFrames / 23.81)})
                               : std::string(PortText("ran its length"));
  Text(Keep(line), UI_FACE_TEXT, Fs(15), 0.0f, kLbInkDesc);
  // Against
  {
    Decl row;
    row.d.layout.childGap = Gap(Fs(10));
    row.d.layout.childAlignment.y = CLAY_ALIGN_Y_CENTER;
    Open(row);
    Text(PortText("Against"), UI_FACE_TEXT, Fs(13), 0.0f, kLbInkDesc);
    std::vector<std::string> items(1, PortText("Nothing"));
    std::vector<int> ids(1, -1);
    int nCur = 0;
    for (size_t k = 0; k < g_runs.size(); ++k) {
      if (static_cast<int>(k) == g_nPick)
        continue;
      items.push_back(ShortWhen(g_runs[k]) + kDot + (g_runs[k].commit.empty() ? "?" : g_runs[k].commit) +
                      (SameGame(a, g_runs[k]) ? std::string(kDot) + PortText("same game") : std::string()));
      ids.push_back(static_cast<int>(k));
      if (static_cast<int>(k) == nOther)
        nCur = static_cast<int>(ids.size()) - 1;
    }
    const int n = Pulldown("bmagainst", 0, 380, items, nCur);
    if (n >= 0)
      g_nAgainst = ids[n];
    Close();
  }
  // the figures
  {
    Decl figs;
    figs.d.layout.sizing.width = Grow();
    figs.d.layout.childGap = Gap(Fs(8));
    Open(figs);
    // a run with no picture has no frames shown to count
    if (a.shown != "none") {
      Figure(PT_("Frames a second"), a.bSummary ? std::to_string(static_cast<int>(a.fps + 0.5)) : kDash, a.fps,
             o ? o->fps : 0.0, true, false);
      Figure(PT_("At the worst 1 %"), a.bSummary ? std::to_string(static_cast<int>(a.fpsWorst + 0.5)) : kDash,
             a.fpsWorst, o ? o->fpsWorst : 0.0, true, false);
    }
    Figure(PT_("Game frames a second"), a.bSummary ? Fixed1(a.gfps) : kDash, a.gfps, o ? o->gfps : 0.0, true, false);
    Figure(PT_("Most units"), a.bSummary ? std::to_string(a.most) : kDash, a.most, o ? o->most : 0.0, true, true);
    Close();
  }
  // the graph
  {
    g_graph.pThis = &a;
    g_graph.pOther = o;
    Decl g;
    g.d.layout.sizing.width = Grow();
    g.d.layout.sizing.height = Fixed(Sc(150));
    static UiCustom graph = {0.0f, DrawRunGraph, NULL};
    g.d.custom.customData = &graph;
    Open(g);
    Close();
    Decl legend;
    legend.d.layout.childGap = Gap(Fs(16));
    legend.d.layout.childAlignment.y = CLAY_ALIGN_Y_CENTER;
    Open(legend);
    Decl sw;
    sw.d.layout.sizing.width = Fixed(Sc(16));
    sw.d.layout.sizing.height = Fixed(Sc(3));
    sw.d.backgroundColor = Col(kBmThis);
    Open(sw);
    Close();
    Text(PortText("this run"), UI_FACE_TEXT, Fs(13), 0.0f, kLbInkDesc);
    if (o != NULL) {
      Decl sw2 = sw;
      sw2.d.backgroundColor = Col(kBmVs);
      Open(sw2);
      Close();
      Text(Keep(ShortWhen(*o) + ", " + (o->commit.empty() ? "?" : o->commit)), UI_FACE_TEXT, Fs(13), 0.0f,
           kLbInkDesc);
    }
    Text(PortText("frame time, ms, over the game"), UI_FACE_TEXT, Fs(13), 0.0f, kLbInkDesc);
    Close();
  }
  PartBars(a, o);
  // the notes
  if (o != NULL) {
    if (SameGame(a, *o)) {
      const bool bSameEnd = !a.endState.empty() && a.endState == o->endState;
      std::string s =
          PortText("The same game: the same map, players, races and seed, at the same speed for the same length.");
      if (!a.endState.empty() && !o->endState.empty())
        // {0} the state both ended in, or {0} and {1} the two states
        s += std::string(" ") +
             (bSameEnd ? PortTextF("Both ended in state {0}, which the replays check frame by frame.",
                                   {a.endState.substr(0, 4) + " " + a.endState.substr(4, 4)})
                       : PortTextF("They ended in different states ({0}, {1}): the builds did not play it the same.",
                                   {a.endState.substr(0, 8), o->endState.substr(0, 8)}));
      Text(Keep(s), UI_FACE_TEXT, Fs(14), 0.0f, a.endState.empty() || o->endState.empty() || bSameEnd ? kBmGood : kLbWarn,
           -1, 1.3f);
    } else {
      Text(PortText("Not the same game, so the numbers say how the two scenarios differ, not how the builds do."),
           UI_FACE_TEXT, Fs(14), 0.0f, kLbWarn, -1, 1.3f);
    }
  }
  Close();
}

// The command line that plays a run's game again with no menus.
std::string RunCommandLine(const Run &r)
{
  const std::string races = RunRaces(r);
  const bool bOne = races == "random" || races == "terran" || races == "zerg" || races == "protoss";
  std::string s = "Hyperion.exe --bench --map \"" + r.map + "\" --players " + std::to_string(r.players);
  s += bOne ? " --computer-race " + races : " --bench-races " + races;
  s += " --seed " + std::to_string(r.seed) + " --bench-frames " + std::to_string(r.frames) + " --bench-speed " +
       r.speed;
  if (r.bFramesCsv)
    s += " --bench-frames-csv";
  return s;
}

void Runs(float fW, float fH)
{
  const float low = fH - 33 - 52 - 10;
  const float listW = (fW - 13 - 34 - 10) * 0.515f;
  RunsList(13, 93, listW, low - 93);
  RunDetail(13 + listW + 10, 93, fW - 34 - 13 - listW - 10, low - 93);
  // the foot: Back, and what can be done with the run picked
  const float y = fH - 33 - 52;
  Decl b = At("bmback", 40, y, 200, 52);
  if (ScButton(b, PortText("Back"), 'B', false))
    g_nResult = FRONT_BENCH_HOME;
  if (g_nPick < 0 || g_nPick >= static_cast<int>(g_runs.size()))
    return;
  const Run &r = g_runs[g_nPick];
  Decl again = At("bmagain", 264, y, 260, 52);
  if (ScButton(again, PortText("Run Again"), 'G', true) && TakeRun(r))
    Start();
  Decl folder = At("bmfolder", 536, y, 330, 52);
  if (ScButton(folder, PortText("Open Log Folder"), 'O', false))
    ShellExecuteA(NULL, "open", PortExplorer(), ("\"" + r.path + "\"").c_str(), NULL, SW_SHOWNORMAL);
  Decl copy = At("bmcopyrun", 878, y, 400, 52);
  if (ScButton(copy, PortText("Copy Command Line"), 'C', false)) {
    SDL_SetClipboardText(RunCommandLine(r).c_str());
    Note(PortText("Copied: the run\xe2\x80\x99s game from a terminal, with no menus."));
  }
  Decl n = At("bmrunsnote", 1302, y, fW - 1302 - 34, 52);
  n.d.layout.childAlignment.y = CLAY_ALIGN_Y_CENTER;
  Open(n);
  Text(PortText("Each run is a folder under benchmarks\\: what was asked, the build, a line a second, the summary and "
                "the replay."),
       UI_FACE_TEXT, Fs(15), 0.0f, kLbInkDesc, -1, 1.25f);
  Close();
}

}  // namespace

namespace FrontKit {

void BenchOpen(bool bRuns)
{
  g_nResult = 0;
  g_bPending = false;
  g_nTab = bRuns ? 1 : 0;
  g_nPictureOf = -1;
  for (MapRead &m : g_maps)
    m.bTried = false;
  ListRuns(bRuns ? PortBenchLastRun() : "");
  FitPlayers();
  g_note.clear();
  FrontLog("bench: open, %u runs\n", static_cast<unsigned>(g_runs.size()));
}

bool BenchCameBack(void)
{
  const std::string last = PortBenchLastRun();
  if (last.empty() || last == g_lastSeen)
    return false;
  g_lastSeen = last;
  return true;
}

bool BenchTake(PortBenchSettings *pOut)
{
  if (!g_bPending)
    return false;
  g_bPending = false;
  *pOut = g_pending;
  return true;
}

int BenchResult(void)
{
  const int n = g_nResult;
  g_nResult = 0;
  return n;
}

void BenchScreen(void)
{
  MapPreviewFrame();
  const float fW = UiWidth() / Sc(1) - 116;
  const float fH = fminf(990.0f, UiHeight() / Sc(1) - 132 - 74);
  Decl panel = Box("bmpanel");
  panel.d.layout.sizing.width = Fixed(Sc(fW));
  panel.d.layout.sizing.height = Fixed(Sc(fH));
  panel.d.floating.attachTo = CLAY_ATTACH_TO_ROOT;
  panel.d.floating.offset.x = Sc(58);
  panel.d.floating.offset.y = Sc(132);
  panel.d.floating.zIndex = 20;
  static UiCustom frame = {0.0f, DrawScFrame, NULL};
  panel.d.custom.customData = &frame;
  Open(panel);
  // the head: the title and what it is, and the tabs
  Decl head = At("bmhead", 40, 22, fW - 40 - 400, 0);
  head.d.layout.layoutDirection = CLAY_TOP_TO_BOTTOM;
  Open(head);
  Text(PortText("Benchmark"), UI_FACE_BOLD, Fs(27), 0.0f, kInkWhite);
  const bool bNote = !g_note.empty() && SDL_GetTicks() - g_nNoteAt < 6000;
  Text(bNote ? Keep(g_note)
             : Keep(std::string(PortText("Up to 64 computers on maps up to 1,024 by 1,024 tiles")) + kDot +
                    PortText("how far this build can be pushed") + kDot + PortText("a work in progress")),
       UI_FACE_TEXT, Fs(17), 0.0f, bNote ? kBmVs : kLbInkInfo);
  Close();
  Decl tabs = Floating("bmtabs", 0, -Sc(34), Sc(32), 22, CLAY_ATTACH_POINT_RIGHT_TOP, CLAY_ATTACH_POINT_RIGHT_TOP);
  tabs.d.layout.childGap = Gap(Fs(8));
  Open(tabs);
  for (int i = 0; i < 2; ++i) {
    Decl t = Box("bmtab", i);
    // {0} how many runs there are
    const std::string label =
        i == 0 ? std::string(PortText("New Run")) : PortTextF("Runs \xc2\xb7 {0}", {std::to_string(g_runs.size())});
    if (ScMiniTab(t, Keep(label), static_cast<char>('1' + i), i == g_nTab))
      g_nTab = i;
  }
  Close();
  if (g_nTab == 0)
    NewRun(fW, fH);
  else
    Runs(fW, fH);
  Close();
  DrawTip();
  PulldownCloseOutside();
}

bool BenchKeys(void)
{
  // a field has the keyboard: the keys are its, but for Tab to the other and Enter or Esc to end
  if (g_nEdit != 0 && g_nTab == 0 && g_s.nLength >= kLengthCount) {
    if (Key(VK_TAB))
      g_nEdit = 3 - g_nEdit;
    else if (Key(VK_RETURN) || Key(VK_ESCAPE)) {
      g_nEdit = 0;
      LengthFields();
    }
    return true;
  }
  if (PulldownKeys())
    return true;
  bool bChanged = false;
  if (g_nTab == 0 && g_nView == 1 && MutatorsTabKeys(MutatorsOfSetUp(), &bChanged))
    return true;
  if (g_nTab == 0 && Key('O'))
    g_nView = 0;
  else if (g_nTab == 0 && Key('U'))
    g_nView = 1;
  else if (Key('1'))
    g_nTab = 0;
  else if (Key('2'))
    g_nTab = 1;
  else if (Key('G') && g_nTab == 0)
    Start();
  else if (Key('G') && g_nTab == 1 && g_nPick >= 0 && g_nPick < static_cast<int>(g_runs.size())) {
    if (TakeRun(g_runs[g_nPick]))
      Start();
  } else if (g_nTab == 1 && Key('O') && g_nPick >= 0 && g_nPick < static_cast<int>(g_runs.size())) {
    ShellExecuteA(NULL, "open", PortExplorer(), ("\"" + g_runs[g_nPick].path + "\"").c_str(), NULL, SW_SHOWNORMAL);
  } else if (g_nTab == 1 && Key('C') && g_nPick >= 0 && g_nPick < static_cast<int>(g_runs.size())) {
    SDL_SetClipboardText(RunCommandLine(g_runs[g_nPick]).c_str());
    Note(PortText("Copied: the run\xe2\x80\x99s game from a terminal, with no menus."));
  } else if (g_nTab == 1 && Key(VK_DOWN) && g_nPick + 1 < static_cast<int>(g_runs.size())) {
    ++g_nPick;
    g_nAgainst = -2;
    if (g_nPick >= g_nFirst + g_nShownRows)
      g_nFirst = g_nPick - g_nShownRows + 1;
  } else if (g_nTab == 1 && Key(VK_UP) && g_nPick > 0) {
    --g_nPick;
    g_nAgainst = -2;
    if (g_nPick < g_nFirst)
      g_nFirst = g_nPick;
  } else if (Key(VK_ESCAPE) || Key('B'))
    g_nResult = FRONT_BENCH_HOME;
  return true;
}

}  // namespace FrontKit
