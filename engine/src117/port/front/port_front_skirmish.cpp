// port_front_skirmish.cpp -- Hyperion's skirmish set-up: Choose a Map, then the set-up laid
// out as SC2's battle lobby (docs/modules/front-end.md). Presentation only: Start Game makes the game as
// 1.17's Play Custom does (do_create_dlg, single player), so the game itself is 1.17's.
//
// The map list is 1.17's own (FillMapList: the data folder's and the install's maps\, each
// --maps folder), a row read as it comes into view (ReadScenarioInfo). Team Match, Hyperion's
// game type after Free For All, sets a team a row by a quick layout from the map's starts
// (port_teams.h) or by hand, and plays by Hyperion's rules (HX_OPT_TEAMS). What the engine
// lacks is greyed with a tip. Another screen can host the set-up (SkirmishHost,
// port_frontscreens.h; the trainer's New Session): its buttons, its box in the
// mutators' place, its own Start taking a SkirmishGame, and a set-up of its own beside this.
#include <windows.h>
#include <math.h>
#include <stdio.h>
#include <ctype.h>
#include <string.h>
#include <algorithm>
#include <deque>
#include <map>
#include <set>
#include <string>
#include <vector>
#include <SDL3/SDL.h>
#include "port_front.h"  // PortFrontSkirmishSpeed
#include "../platform/port_paths.h"  // PortMapFolderCount, PortMapFolderPath, PortCommandOptions
#include "oc_data.h"         // OC_VA, where 1.17's data is
#include "Storm.h"           // SStrCopy
#include "gluMap.h"          // LISTENTRY, ReadScenarioInfo, FindMapFile
#include "gluBNCreate.h"     // FillMapList, CreateNewGame
#include "gametype.h"        // the templates
#include "glues.h"           // mem_free
#include "maphdr.h"          // get_map_chk
#include "net_slot.h"        // slot_set_race
#include "playertables.h"    // PlayersMapSlot
#include "../../h/playerlimits.h"  // g_hxPlayers, PlayerSlots
#include "port_frontkit.h"
#include "port_frontlobby.h"  // the lobby's pieces, a map's picture
#include "port_front_mutators.h"  // the Mutators tab
#include "port_front_colours.h"   // the colours a row may take
#include "port_frontscreens.h"
#include "../data/port_moddata.h"    // the mod's races
#include "../gameplay/port_teams.h"      // the teams, their layouts and shades
#include "../lan/port_lanclient.h"  // LanClientRoom
#include "../data/port_mods.h"
#include "../../h/hxrules.h"            // RulesChosenClassic
#include "port_ui.h"
#include "port_world.h"     // the camera's view
#include "snd.h"             // load_game_data, load_array_data
#include "rect16.h"          // gUnitBoundBox's rows
#include "../../gen/globals117.h"  // gUnitBoundBox
#include "../gameplay/port_ai.h"         // the computers' levels
#include "../gameplay/port_handicap.h"   // the handicaps
#include "../gameplay/port_mutators.h"   // the mutators
#include "../../h/mutators.h"            // MUTATOR_RICH_*
#include "../gameplay/port_watchgame.h"    // PortWatchBegin, a watched game
#include "../data/port_stormapi.h"  // SFileGetUserDataPath
#include "../platform/port_globals117.h"
#include "../render/port_maptext.h"  // MapTextUtf8

using namespace FrontKit;

namespace {

enum { RACE_ZERG = 0, RACE_TERRAN = 1, RACE_PROTOSS = 2, RACE_RANDOM = 6, RACE_NONE = 7, RACE_MOD = 0x100 };
enum { STEP_MAPS, STEP_SETUP };
// A room's lobby has two more: a seat open to the room's people, and one of them.
enum { KIND_YOU, KIND_COMPUTER, KIND_CLOSED, KIND_OPEN, KIND_PERSON };

// The computer levels' tip (port_ai.h).
const char kLevelTip[] = PT_(
    "Classic AI is the original\xe2\x80\x99s own computer, and plays by either rules. The others play by "
    "Hyperion\xe2\x80\x99s rules: StarCraft II\xe2\x80\x99s levels, Very Easy to Elite, from slower and smaller to "
    "faster, and the Cheater levels, which play as Elite and cheat as StarCraft II\xe2\x80\x99s do. Vision sees "
    "the whole map; resources sees it too and brings back more from each trip, growing to half as much "
    "again by 20 minutes; insane brings back a fifth more from the start, growing to twice as much.");

// The handicap's tip (port_handicap.h).
const char kHandicapTip[] = PT_(
    "The player\xe2\x80\x99s units and buildings at this share of their life and shields, as in StarCraft II, "
    "to even a game between players of different skill. Damage, energy and speed stay as they are. Below "
    "100% the game plays by Hyperion\xe2\x80\x99s rules.");

const char *const kSpeeds[] = {PT_("Slowest"), PT_("Slower"), PT_("Slow"),  PT_("Normal"),
                               PT_("Fast"),    PT_("Faster"), PT_("Fastest")};
const char *const kTilesets[] = {PT_("Badlands"), PT_("Space Platform"), PT_("Installation"), PT_("Ash World"),
                                 PT_("Jungle"),   PT_("Desert"),         PT_("Arctic"),       PT_("Twilight")};
// 1.17's lobby colours, red to tan: the first a row is given and Distinct Colours chooses from
const int kColourCount = 11;
const float kSwatchW = 150;  // a row's colour tile, wide enough for "Pale Yellow" on one line
const char *const kFilters[] = {PT_("All"), "2", "3\xe2\x80\x93" "4", "5\xe2\x80\x93" "8", "9\xe2\x80\x93" "16",
                                 "17\xe2\x80\x93" "32"};
const int kFilterLo[] = {0, 2, 3, 5, 9, 17};
const int kFilterHi[] = {999, 2, 4, 8, 16, 32};
// the size filter, by the map's longer side in tiles: 1.17's sizes, to 256, and past it
const char *const kSizes[] = {PT_("Any size"), PT_("To 128"), PT_("To 256"), PT_("Past 256")};
const int kSizeLo[] = {0, 0, 129, 257};
const int kSizeHi[] = {99999, 128, 256, 99999};

// A row of the map list: 1.17's entry, which lives until the list is made again.
struct Row {
  LISTENTRY *p;
  std::string text;
  int nPast8 = 0;  // a Hyperion map's players past 1.17's 8 (its HXPL), counted as it is read
};

struct Slot {
  int nKind;
  int nRace;    // RACE_*, or RACE_MOD + a mod's race
  int nColour;  // one of the game's 65 (port_front_colours.h), COLOUR_ANY its own, or -1 the slot's own
  int nTeam = 0;     // g_teamNames; with one name, no teams
  int nLevel = AI_LEVEL_CLASSIC;  // a computer's level (port_ai.h)
  int nHandicap = 100;  // the share of its units' life and shields, 50 to 100 (port_handicap.h)
  unsigned rgb = 0;  // the colour by team (g_nColourBy 1), a shade of the team's
  unsigned any = 0;  // its own colour (COLOUR_ANY), in full RGB
  // a room's person (KIND_PERSON): their name, the seat chip, Ready, and whether it is this
  // player's own seat (a guest's)
  std::string name, chip;
  bool bReady = false, bYou = false;
};

struct GameType {
  std::string name;
  TGAMEPARAMS params;
  bool bTeamMatch = false;  // Hyperion's Team Match: Free For All's template, with teams
};

// The map the set-up is for, held apart from the list, which is made again as the browser
// moves (1.17's entries live until then).
struct MapInfo {
  std::string name;   // the entry's szName, the path the game opens
  std::string file;   // the file's name
  std::string dir;    // the folder it was listed in
  std::string title;
  std::string desc;
  int w = 0, h = 0, tileset = 0, players = 0;
};

// The maps under one of the list's folders, counted on the disk.
struct FolderMaps {
  int n = 0;
  std::vector<std::string> names;  // by name, without the extension
};

int g_nStep;
char g_szDir[0x104] = "maps";       // the folder the list shows
std::vector<Row> g_rows;
std::vector<FolderMaps> g_folderMaps;  // each row's, for the folders
FolderMaps g_hereMaps;              // the folder shown
int g_nPick = -1;                   // the row picked
int g_nFirst;                       // the first row shown
int g_nShown = 12;                  // how many rows the list showed last frame
int g_nFilter;
int g_nSize;
std::string g_note;                 // what went wrong, under the header or the list
int g_nResult;                      // what the screen chose (port_frontscreens.h)
bool g_bPlayed;                     // a game was made: the screen comes back on its set-up

// the set-up
MapInfo g_map;                      // its map, name "" before one is chosen
MapPicture g_pic;                   // the map last read: its starts and minimap
std::vector<Slot> g_slots;
std::string g_slotsFor;  // the profile the slots were set for
int g_nSlotFirst;                   // the first slot shown
int g_nSlotShown = 8;               // how many slots fit, measured each frame
std::vector<GameType> g_types;
int g_nType;
std::vector<GameType> g_subs;       // the type's sub-types, when it has them
int g_nSub;
int g_nSpeed = 6;
int g_nWatchMul = 1;  // a watched game's speed past Fastest, x2 to x16 (port_watchgame.h)
int g_nRules = -1;                  // the Rules pulldown: 1 Hyperion, 0 classic, -1 not read yet
MutatorSet g_mut;                   // the mutators, 1.17's when none is chosen (port_mutators.h)
int g_nView;                        // the right column's tab: 0 Game Options and the map, 1 Mutators
int g_nDensity;                     // the slot list: 0 auto, 1 rows, 2 compact
bool g_bFolded;                     // the players' frame folded
bool g_bBrowse;                     // Change Map's browser, over the set-up
bool g_bEnlarge;                    // the map's picture, enlarged
// Teams (port_teams.h): the teams' names, one for none; the layout chosen from the map's
// (Layouts, -1 by hand); the starts (0 random, 1 by slot); the colours (0 by player, 1 by team)
std::vector<std::string> g_teamNames = {"Everyone"};
int g_nLayout;
int g_nStartsBy;
int g_nColourBy;
int g_nFrames = 1;                  // the team frames drawn last frame

// The screen hosting the set-up, NULL for Skirmish; each keeps its own set-up, held while the
// other shows. What is read from the disk (the list, the map's picture) is read again.
const SkirmishHost *g_pHost;
struct Held {
  int nStep = STEP_MAPS;
  std::string dir = "maps";
  int nPick = -1, nFirst = 0, nFilter = 0, nSize = 0;
  MapInfo map;
  std::vector<Slot> slots;
  std::string slotsFor;
  int nSlotFirst = 0;
  std::string type;
  int nSub = 0, nSpeed = 6, nRules = -1;
  MutatorSet mut;
  std::vector<std::string> teamNames = {"Everyone"};
  int nLayout = 0, nStartsBy = 0, nColourBy = 0;
  bool bPlayed = false;
};
std::map<std::string, Held> g_held;

// A room's lobby (SkirmishHost nRoom): 0 none, 1 the host's, 2 a guest's, who sees the host's
// set-up and changes only their own race.
int RoomMode(void)
{
  return g_pHost != NULL ? g_pHost->nRoom : 0;
}

bool Guest(void)
{
  return RoomMode() == 2;
}

// A slot someone plays in: a computer, this player, or a room's person.
bool Seated(const Slot &s)
{
  return s.nKind != KIND_CLOSED && s.nKind != KIND_OPEN;
}

bool Mine(const Slot &s)
{
  return s.nKind == KIND_YOU || (s.nKind == KIND_PERSON && s.bYou);
}

// Skirmish's own set-up, not a host's, may make the player's seat a computer's: the player
// watches a game of computers (port_watchgame.h).
bool WatchOffered(void)
{
  return g_pHost == NULL;
}

bool Watching(void)
{
  return WatchOffered() && !g_slots.empty() && g_slots[0].nKind == KIND_COMPUTER;
}

// when this player last chose their race: the room's word on it waits until it has heard
Uint64 g_qwRaceAt;
// and their handicap, likewise
Uint64 g_qwHandicapAt;

Held HoldSetUp(void)
{
  Held h;
  h.nStep = g_nStep;
  h.dir = g_szDir;
  h.nPick = g_nPick;
  h.nFirst = g_nFirst;
  h.nFilter = g_nFilter;
  h.nSize = g_nSize;
  h.map = g_map;
  h.slots = g_slots;
  h.slotsFor = g_slotsFor;
  h.nSlotFirst = g_nSlotFirst;
  h.type = g_nType < static_cast<int>(g_types.size()) ? g_types[g_nType].name : std::string();
  h.nSub = g_nSub;
  h.nSpeed = g_nSpeed;
  h.nRules = g_nRules;
  h.mut = g_mut;
  h.teamNames = g_teamNames;
  h.nLayout = g_nLayout;
  h.nStartsBy = g_nStartsBy;
  h.nColourBy = g_nColourBy;
  h.bPlayed = g_bPlayed;
  return h;
}

void TakeSetUp(const Held &h)
{
  g_nStep = h.nStep;
  SStrCopy(g_szDir, h.dir.c_str(), sizeof g_szDir);
  g_nPick = h.nPick;
  g_nFirst = h.nFirst;
  g_nFilter = h.nFilter;
  g_nSize = h.nSize;
  g_map = h.map;
  g_slots = h.slots;
  g_slotsFor = h.slotsFor;
  g_nSlotFirst = h.nSlotFirst;
  g_nSub = h.nSub;
  g_nSpeed = h.nSpeed;
  g_nRules = h.nRules;
  g_mut = h.mut;
  g_teamNames = h.teamNames;
  g_nLayout = h.nLayout;
  g_nStartsBy = h.nStartsBy;
  g_nColourBy = h.nColourBy;
  g_bPlayed = h.bPlayed;
  g_pic = MapPicture();
}

bool IsFolder(const Row &r)
{
  return (r.p->bFlags & 8) != 0;
}

bool IsMap(const Row &r)
{
  return (r.p->bFlags & 0x8F) == 4;
}

std::string Title(const Row &r)
{
  if (IsFolder(r)) {
    std::string s = r.text;
    if (s.size() > 2 && s.front() == '[' && s.back() == ']')
      s = s.substr(1, s.size() - 2);
    return s == ".." ? std::string("Up One Level") : s;
  }
  // a map's title as StarCraft: Remastered reads it, UTF-8 or a Korean map's CP949
  return r.p->dwLoaded && r.p->szStatus[0] ? MapTextUtf8(r.p->szStatus) : r.text;
}

bool IsUp(const Row &r)
{
  return IsFolder(r) && Title(r) == "Up One Level";
}

int __stdcall AddRow(intptr_t nItem, intptr_t nText, int)
{
  Row r;
  r.p = reinterpret_cast<LISTENTRY *>(nItem);
  r.text = reinterpret_cast<const char *>(nText);
  g_rows.push_back(r);
  return static_cast<int>(g_rows.size()) - 1;
}

// The maps under pszDir (a folder as the list names it, "maps\..."), from each place the
// list reads (FillMapList: the data folder, the install, each --maps folder) and the folders
// in them, each file once by its path; StarCraft leaves out Brood War's .scx.
void WalkMaps(const std::string &dir, const std::string &rel, int nDepth, std::set<std::string> *pSeen,
              FolderMaps *pOut)
{
  if (nDepth > 8 || pSeen->size() > 5000)
    return;
  WIN32_FIND_DATAA fd;
  const HANDLE h = FindFirstFileA((dir + "\\*").c_str(), &fd);
  if (h == INVALID_HANDLE_VALUE)
    return;
  do {
    const std::string name = fd.cFileName;
    if (name == "." || name == "..")
      continue;
    if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
      WalkMaps(dir + "\\" + name, rel + name + "\\", nDepth + 1, pSeen, pOut);
      continue;
    }
    const size_t nDot = name.rfind('.');
    if (nDot == std::string::npos)
      continue;
    const char *pszExt = name.c_str() + nDot + 1;
    if (lstrcmpiA(pszExt, "scm") != 0 && lstrcmpiA(pszExt, "hxm") != 0 &&
        (lstrcmpiA(pszExt, "scx") != 0 || !gbIsExpansion))
      continue;
    std::string key = rel + name;
    CharLowerA(&key[0]);
    if (!pSeen->insert(key).second)
      continue;
    ++pOut->n;
    pOut->names.push_back(name.substr(0, nDot));
  } while (FindNextFileA(h, &fd));
  FindClose(h);
}

void CountMaps(const char *pszDir, FolderMaps *pOut)
{
  *pOut = FolderMaps();
  std::vector<std::string> roots;
  char sz[0x104];
  SFileGetUserDataPath(sz, sizeof sz, 1);
  SStrPack(sz, pszDir, sizeof sz);
  roots.push_back(sz);
  SFileGetInstallPath(sz, sizeof sz, 1);
  SStrPack(sz, pszDir, sizeof sz);
  if (lstrcmpiA(sz, roots[0].c_str()) != 0)
    roots.push_back(sz);
  for (int i = 0; i < PortMapFolderCount(); ++i)
    if (PortMapFolderPath(i, pszDir, sz, sizeof sz))
      roots.push_back(sz);
  std::set<std::string> seen;
  for (const std::string &root : roots)
    WalkMaps(root, "", 0, &seen, pOut);
  std::sort(pOut->names.begin(), pOut->names.end(),
            [](const std::string &a, const std::string &b) { return lstrcmpiA(a.c_str(), b.c_str()) < 0; });
}

// The list for the folder g_szDir, as Play Custom makes it (sub_455F50): the first map
// picked, else the first entry (ClearLBox); and how many maps each folder holds.
void ListFolder(void)
{
  g_rows.clear();
  const int bAtRoot = lstrcmpiA(g_szDir, "maps") == 0;
  FillMapList(0x28, g_szDir, bAtRoot, AddRow, NULL);
  g_nPick = -1;
  for (size_t i = 0; i < g_rows.size() && g_nPick < 0; ++i)
    if (IsMap(g_rows[i]))
      g_nPick = static_cast<int>(i);
  if (g_nPick < 0 && !g_rows.empty())
    g_nPick = 0;
  g_nFirst = 0;
  g_folderMaps.assign(g_rows.size(), FolderMaps());
  for (size_t i = 0; i < g_rows.size(); ++i)
    if (IsFolder(g_rows[i]) && !IsUp(g_rows[i]))
      CountMaps(g_rows[i].p->szName, &g_folderMaps[i]);
  CountMaps(g_szDir, &g_hereMaps);
}

bool g_bReveal;  // scroll the list to the picked row on the next frame

// Into a folder, or up from one: going up picks the folder it came from, as a file
// browser does, so Back and Enter retrace the steps.
void OpenFolder(const char *pszDir)
{
  char szFrom[0x104];
  SStrCopy(szFrom, g_szDir, sizeof szFrom);
  SStrCopy(g_szDir, pszDir, sizeof g_szDir);
  ListFolder();
  const size_t n = strlen(g_szDir);
  if (_strnicmp(szFrom, g_szDir, n) != 0 || szFrom[n] != '\\')
    return;
  // the child of this folder on the way to the one left
  if (char *p = strchr(szFrom + n + 1, '\\'))
    *p = 0;
  for (size_t i = 0; i < g_rows.size(); ++i)
    if (IsFolder(g_rows[i]) && lstrcmpiA(g_rows[i].p->szName, szFrom) == 0) {
      g_nPick = static_cast<int>(i);
      g_bReveal = true;
    }
}

// The set-up's map picked in the list, in its own folder.
void ShowSetUpMap(void)
{
  if (g_map.name.empty())
    return;
  if (lstrcmpiA(g_szDir, g_map.dir.c_str()) != 0) {
    SStrCopy(g_szDir, g_map.dir.c_str(), sizeof g_szDir);
    ListFolder();
  }
  for (size_t i = 0; i < g_rows.size(); ++i)
    if (IsMap(g_rows[i]) && g_map.name == g_rows[i].p->szName) {
      g_nPick = static_cast<int>(i);
      g_bReveal = true;
    }
}

// The row's details, read once (Play Custom reads them when a row is picked). 1.17's
// count of a map's players stops at 8; the slots a Hyperion map gives past them are its
// HXPL's, read with it, which count as CountValidSlots counts.
void ReadRow(Row &r)
{
  if (!IsMap(r) || r.p->dwLoaded != 0)
    return;
  ReadScenarioInfo(r.p);
  r.nPast8 = 0;
  if (g_hxPlayers && r.p->dwLoading == 0)
    for (int s = 8; s < PlayerSlots(); ++s) {
      const unsigned char bOwner = PlayersMapSlot(s)->bOwner;
      r.nPast8 += s != 11 && (bOwner == 3 || bOwner == 4 || bOwner == 5 || bOwner == 6 || bOwner == 7);
    }
}

int Players(const Row &r)
{
  return r.p->dwLoaded && r.p->dwLoading == 0 ? r.p->bField479 + r.nPast8 : 0;
}

bool Passes(const Row &r)
{
  if ((g_nFilter == 0 && g_nSize == 0) || !IsMap(r) || !r.p->dwLoaded)
    return true;
  const int n = Players(r);
  const int nSide = r.p->wWidth > r.p->wHeight ? r.p->wWidth : r.p->wHeight;
  return n >= kFilterLo[g_nFilter] && n <= kFilterHi[g_nFilter] && nSide >= kSizeLo[g_nSize] &&
         nSide <= kSizeHi[g_nSize];
}

std::string TilesetName(const Row &r)
{
  if (!r.p->dwLoaded)
    return "";
  const int t = r.p->wTileset & 7;
  return PortText(kTilesets[t]);
}

// A map's starts and minimap, from its scenario.chk (MapPictureRead). The last map read
// stays read.
void ReadStarts(const char *pszName)
{
  if (g_pic.of == pszName)
    return;
  g_pic = MapPicture();
  g_pic.of = pszName;
  char szPath[0x104];
  if (!FindMapFile(pszName, szPath, sizeof szPath))
    return;
  DWORD dwSize = 0;
  BYTE *pChk = static_cast<BYTE *>(get_map_chk(szPath, &dwSize));
  if (pChk == NULL)
    return;
  MapPictureRead(&g_pic, pszName, pChk, dwSize);
  mem_free(pChk, __FILE__, __LINE__);
}

void __stdcall AddType(const char *pszName, const char *, const TGAMEPARAMS *pParams, const TTEMPLATEMETA *)
{
  if (!gametype_template_allowed(reinterpret_cast<const BYTE *>(pParams), 0))
    return;
  GameType t;
  t.name = pszName;
  t.params = *pParams;
  g_types.push_back(t);
}

void __stdcall AddSub(const char *, const char *, const TGAMEPARAMS *pParams, const TTEMPLATEMETA *)
{
  if (pParams->dwSubValue == 0)
    return;
  GameType t;
  char sz[64] = "";
  gametype_get_subtype_name(sz, sizeof sz, pParams, static_cast<int>(g_slots.size()));
  t.name = sz;
  t.params = *pParams;
  // Top vs Bottom splits a map's players; a split needs a player on each side
  if (pParams->bCategory == 0x0F && pParams->dwSubValue >= g_slots.size())
    return;
  g_subs.push_back(t);
}

void ListSubs(void)
{
  g_subs.clear();
  g_nSub = 0;
  if (g_nType < static_cast<int>(g_types.size()))
    IterateLeagueTemplates(g_types[g_nType].params.bCategory, g_types[g_nType].params.bSub, AddSub);
}

bool UseMapSettings(void)
{
  return g_nType < static_cast<int>(g_types.size()) && g_types[g_nType].params.bCategory == 10;
}

// 1.17's Free For All among the types (not Team Match on its template), or -1.
int FreeForAll(void)
{
  for (size_t i = 0; i < g_types.size(); ++i)
    if (g_types[i].params.bCategory == 3 && !g_types[i].bTeamMatch)
      return static_cast<int>(i);
  return -1;
}

// A row's first colour: 1.17's of its slot for the first eight, past them the one Hyperion
// gives a slot past 1.17's (PlayersDefaultColour), none of those eight and no two alike.
int RowColour(size_t i)
{
  return i < 8 ? static_cast<int>(i) : PlayersDefaultColour(static_cast<int>(i) - 8);
}

// Whether the rows choose their colours: in a single-player game of a melee template, team
// ones too, the computers are seated one by one and then shuffled to the starts, which the
// game follows (PlayersSeatedComputer); not with Use Map Settings, whose map gives them.
bool ColoursChosen(void)
{
  // a room's game seats its players in 1.17's own lobby, which gives each slot its colour
  if (RoomMode() != 0 || g_nType >= static_cast<int>(g_types.size()))
    return false;
  const TGAMEPARAMS &t = g_types[g_nType].params;
  return IsMeleeTemplate(t.bCategory, t.bSub, t.wType);
}

// Whether the rows choose their starts: as they choose colours, in a game type that makes no
// teams of its own (1.17's team types list them as its sub-types, Top vs Bottom its split).
bool StartsOffered(void)
{
  return ColoursChosen() && g_subs.empty() && !UseMapSettings();
}

// Team Match, the game type with teams: the rows choose them.
bool TeamsOffered(void)
{
  return g_nType < static_cast<int>(g_types.size()) && g_types[g_nType].bTeamMatch && StartsOffered();
}

bool HasTeams(void)
{
  return TeamsOffered() && g_teamNames.size() > 1;
}

// The map's starts known for every row: then starts by slot and layouts from the map.
bool StartsKnown(void)
{
  return g_pic.of == g_map.name && g_pic.w > 0 && g_pic.starts.size() == g_slots.size();
}

// The layouts for the map's starts (MapLayouts), in the log when worked out.
const std::vector<TeamLayout> &Layouts(void)
{
  bool bNew;
  const std::vector<TeamLayout> &ls =
      MapLayouts(StartsKnown() ? &g_pic : NULL, static_cast<int>(g_slots.size()), &bNew);
  if (bNew)
    for (const TeamLayout &l : ls)
      FrontLog("skirmish: layout %s, %s, score %.3f\n", l.name.c_str(), l.vs.c_str(), l.fScore);
  return ls;
}

unsigned TeamBase(int q)
{
  return kTeamBases[q % 16];
}

// Colours by team: each team's seated rows shades of its colour, in row order.
void TeamColours(void)
{
  for (size_t q = 0; q < g_teamNames.size(); ++q) {
    std::vector<size_t> ids;
    for (size_t i = 0; i < g_slots.size(); ++i)
      if (g_slots[i].nTeam == static_cast<int>(q) && g_slots[i].nKind != KIND_CLOSED)
        ids.push_back(i);
    const std::vector<unsigned> shades = TeamsShades(TeamBase(static_cast<int>(q)), static_cast<int>(ids.size()));
    for (size_t k = 0; k < ids.size(); ++k)
      g_slots[ids[k]].rgb = shades[k];
  }
}

void TeamsChanged(void)
{
  if (g_nColourBy == 1 && HasTeams())
    TeamColours();
}

// No teams: every row on the one, Everyone.
void NoTeams(void)
{
  for (Slot &s : g_slots)
    s.nTeam = 0;
  g_teamNames = {"Everyone"};
  g_nLayout = 0;
  g_nColourBy = 0;
}

// A layout from the map's (Layouts): its teams and their names, and the starts by slot, so
// each player starts in their team's part of the map; Free for all leaves the starts.
void ApplyLayout(int n)
{
  const std::vector<TeamLayout> &ls = Layouts();
  if (n < 0 || n >= static_cast<int>(ls.size()))
    return;
  const TeamLayout &l = ls[n];
  if (l.bFfa) {
    NoTeams();
  } else {
    for (size_t i = 0; i < g_slots.size() && i < l.teams.size(); ++i)
      g_slots[i].nTeam = l.teams[i];
    g_teamNames = l.names;
    g_nStartsBy = 1;
  }
  g_nLayout = n;
  TeamsChanged();
}

// Team Match's first teams, coloured by team: the map's first layout of teams (it starts each
// player in their team's part), else the seated rows split in two, the first half Team 1.
void FirstTeams(void)
{
  g_nColourBy = 1;
  const std::vector<TeamLayout> &ls = Layouts();
  for (size_t k = 0; k < ls.size(); ++k)
    if (!ls[k].bFfa) {
      ApplyLayout(static_cast<int>(k));
      return;
    }
  int nSeated = 0, n = 0;
  for (const Slot &s : g_slots)
    nSeated += s.nKind != KIND_CLOSED;
  for (Slot &s : g_slots)
    s.nTeam = s.nKind != KIND_CLOSED && n++ >= (nSeated + 1) / 2 ? 1 : 0;
  g_teamNames = {"Team 1", "Team 2"};
  g_nLayout = -1;
  TeamsChanged();
}

// A row joins team q, a new one when q is past the last; the layout is then by hand.
void JoinTeam(size_t i, int q)
{
  if (q >= static_cast<int>(g_teamNames.size())) {
    if (g_teamNames.size() == 1)
      g_teamNames[0] = "Team 1";
    q = TeamsAdd(&g_teamNames);
  }
  g_slots[i].nTeam = q;
  g_nLayout = -1;
  TeamsChanged();
}

// The set-up's row for slot i, as the game counts them (PlayersChooseColour): you 0, then the
// computers in turn; -1 for a closed slot.
int SeatRow(size_t i)
{
  if (i == 0)
    return 0;
  if (g_slots[i].nKind != KIND_COMPUTER)
    return -1;
  int n = 1;
  for (size_t k = 1; k < i; ++k)
    n += g_slots[k].nKind == KIND_COMPUTER;
  return n;
}

// The colour a row chose: one of the game's, or its own.
unsigned Chosen(const Slot &s)
{
  return s.nColour == COLOUR_ANY ? s.any : ColourRgb(s.nColour);
}

// A row's colour as the screen shows it: its team's shade, or the colour it chose.
unsigned RowRgb(const Slot &s)
{
  return g_nColourBy == 1 && HasTeams() && s.rgb != 0 ? s.rgb : s.nColour >= 0 ? Chosen(s) : 0xb4b4b4;
}

// A row's colour's name: the game's name for it, or its own colour's hex.
std::string ColourWord(const Slot &s)
{
  if (s.nColour != COLOUR_ANY)
    return ColourName(s.nColour);
  char sz[16];
  SDL_snprintf(sz, sizeof sz, "#%06X", s.any & 0xffffff);
  return sz;
}

// The rows as they stand, in the log (and so in a crash report's last notes): each row's
// kind, colour and race.
void NoteSlots(const char *pszWhy)
{
  std::string s;
  for (size_t i = 0; i < g_slots.size(); ++i) {
    const Slot &r = g_slots[i];
    char sz[64];
    SDL_snprintf(sz, sizeof sz, "%s%u %s %s race %d team %d handicap %d", i ? ", " : "", static_cast<unsigned>(i + 1),
                 r.nKind == KIND_YOU ? "you" : r.nKind == KIND_COMPUTER ? "computer" : r.nKind == KIND_OPEN ? "open"
                 : r.nKind == KIND_PERSON ? r.name.c_str() : "closed",
                 r.nColour >= 0 ? ColourWord(r).c_str() : "-", r.nRace, r.nTeam,
                 r.nHandicap);
    s += sz;
  }
  FrontLog("skirmish: %s: %s\n", pszWhy, s.c_str());
}

std::string PathLabel(const char *pszDir)
{
  std::string s = pszDir;
  if (s.size() >= 4)
    s = "Maps" + s.substr(4);
  return s + "\\";
}

// What kind of map a file is, by its extension.
const char *MapKind(const std::string &file)
{
  const size_t nDot = file.rfind('.');
  const char *pszExt = nDot == std::string::npos ? "" : file.c_str() + nDot + 1;
  if (lstrcmpiA(pszExt, "scx") == 0)
    return PortText("Brood War map (.scx)");
  if (lstrcmpiA(pszExt, "hxm") == 0)
    return PortText("Hyperion map (.hxm)");
  return PortText("StarCraft map (.scm)");
}

// A map past what 1.17 holds, which plays only by Hyperion's rules.
bool BigMap(int nPlayers, int w, int h)
{
  return nPlayers > 8 || w > 256 || h > 256;
}

MapInfo InfoOf(const Row &r)
{
  MapInfo m;
  m.name = r.p->szName;
  m.file = r.text;
  m.dir = g_szDir;
  m.title = Title(r);
  m.desc = MapTextUtf8(r.p->szDescription);
  m.w = r.p->wWidth;
  m.h = r.p->wHeight;
  m.tileset = r.p->wTileset & 7;
  m.players = g_pic.of == m.name && !g_pic.starts.empty() ? static_cast<int>(g_pic.starts.size()) : Players(r);
  return m;
}

// The profile's race last played as a slot's race (front.js lastRace), Random the first time
// or when it was a mod's race and that mod is not active; and a race by that name.
int LastRace(void)
{
  const std::string s = ProfileLastRace();
  if (s == "zerg")
    return RACE_ZERG;
  if (s == "terran")
    return RACE_TERRAN;
  if (s == "protoss")
    return RACE_PROTOSS;
  const int nMod = ModDataId()[0] != '\0' && !s.empty() && s != "random" ? ModRaceByName(s.c_str()) : 0;
  return nMod > 0 ? RACE_MOD + nMod : RACE_RANDOM;
}

const char *RaceKeep(int nRace)
{
  if (nRace >= RACE_MOD)
    return ModRaceName(nRace - RACE_MOD);
  return nRace == RACE_ZERG ? "zerg" : nRace == RACE_TERRAN ? "terran" : nRace == RACE_PROTOSS ? "protoss" : "random";
}

// The choice of map leads to the set-up: a slot a start, the local player first. The same
// map keeps its set-up.
void SetUp(int nRow)
{
  Row &r = g_rows[nRow];
  ReadRow(r);
  if (r.p->dwLoading == 1) {
    g_note = PortText("That map could not be read.");
    return;
  }
  if (r.p->dwLoading == 2) {
    g_note = PortText("That map has more players than Hyperion holds.");
    return;
  }
  ReadStarts(r.p->szName);
  g_note.clear();
  if (g_map.name != r.p->szName || g_slots.empty()) {
    g_map = InfoOf(r);
    const int nStarts = g_map.players;
    // a room's seats start open to its people
    g_slots.assign(nStarts > 1 ? nStarts : 2, Slot{RoomMode() != 0 ? KIND_OPEN : KIND_COMPUTER, RACE_RANDOM, -1});
    g_slots[0] = Slot{KIND_YOU, LastRace(), -1};
    g_slotsFor = ProfileCurrent();
    for (size_t i = 0; i < g_slots.size(); ++i)
      g_slots[i].nColour = RowColour(i);
    g_nSlotFirst = 0;
    NoTeams();
    g_nStartsBy = 0;
    ListSubs();
  }
  g_nStep = STEP_SETUP;
  g_lbOpen.clear();
  g_bBrowse = false;
  NoteSlots(g_map.name.c_str());
}

// Why the game must play by Hyperion's rules (h/hxrules.h), or "" when either
// will do.
std::string ForcedWhy(void)
{
  if (ModDataId()[0] != '\0')
    return PortTextF("the mod {0}, which brings its own data", {ModDataId()});
  if (g_slots.size() > 8)
    return PortTextF("{0} players (the original allows 8)", {std::to_string(g_slots.size())});
  if (g_map.w > 256 || g_map.h > 256)
    return PortTextF("a {0} \xc3\x97 {1} map (the original allows 256)",
                     {std::to_string(g_map.w), std::to_string(g_map.h)});
  if (HasTeams())
    return PortText("Team Match (the original has no teams in a game against computers)");
  for (size_t i = 0; i < g_slots.size(); ++i)
    if (g_slots[i].nKind == KIND_COMPUTER && g_slots[i].nLevel != AI_LEVEL_CLASSIC && !UseMapSettings())
      return PortTextF("a computer at {0} (the original has one computer, its own)",
                       {PortText(AiLevelName(g_slots[i].nLevel))});
  for (const Slot &s : g_slots)
    if (Seated(s) && s.nHandicap < 100)
      return PortText("a handicap (the original has none)");
  // the unit limit (Game Options) alone, or with other mutators
  MutatorSet rest = g_mut;
  rest.nUnits = 0;
  if (MutatorsStandard(rest) && g_mut.nUnits > 0)
    return PortTextF("a unit limit of {0} (the original holds 1,700)",
                     {MutatorUnitLimitName(MutatorUnitLimitOf(g_mut.nUnits))});
  if (!MutatorsStandard(g_mut))
    return PortTextF("mutators ({0})", {MutatorsText(g_mut, true)});
  return "";
}

bool HyperionRules(void)
{
  // a guest's g_nRules is the host's (SkirmishGuestSetUp)
  return !ForcedWhy().empty() || g_nRules == 1;
}

const char *CreateError(int nResult)
{
  switch (static_cast<unsigned>(nResult)) {
  case 0x80000007: return PortText("That map could not be read.");
  case 0x80000001: return PortText("That map has more players than Hyperion holds.");
  case 0x8000000C:
  case 0x8000000D: return PortText("That map has no room for a computer opponent in this game type.");
  case 0x80000008: return PortText("That game type does not fit this map.");
  default: return PortText("The game could not be made.");
  }
}

// The set-up's map in the list, read: the list moves with the browser, so its folder is
// listed again when the map is not in the one shown.
LISTENTRY *MapEntry(void)
{
  for (int nPass = 0; nPass < 2; ++nPass) {
    for (size_t i = 0; i < g_rows.size(); ++i)
      if (IsMap(g_rows[i]) && g_map.name == g_rows[i].p->szName) {
        ReadRow(g_rows[i]);
        return g_rows[i].p;
      }
    SStrCopy(g_szDir, g_map.dir.c_str(), sizeof g_szDir);
    ListFolder();
  }
  return NULL;
}

// The set-up as a record (port_frontscreens.h), with nComputers seats past yours; the map's
// path aside, which only a game made later needs.
SkirmishGame SkirmishRecord(int nComputers)
{
  // watched, Melee plays as Free For All: a single-player Melee puts every computer on one
  // side, against the player's seat
  const int nFfa = Watching() && g_types[g_nType].params.bCategory == 2 ? FreeForAll() : -1;
  const TGAMEPARAMS &t = nFfa >= 0 ? g_types[nFfa].params
                         : g_subs.empty() ? g_types[g_nType].params : g_subs[g_nSub].params;
  SkirmishGame g;
  g.title = g_map.title;
  g.type = nFfa >= 0 ? g_types[nFfa].name
                     : g_types[g_nType].name + (g_subs.empty() ? std::string() : " " + g_subs[g_nSub].name);
  g.nKey = t.bCategory | t.bSub << 8 | t.wType << 16;
  g.bMelee = IsMeleeTemplate(t.bCategory, t.bSub, t.wType) != 0;
  g.bColours = ColoursChosen();
  g.nSpeed = g_nSpeed;
  g.bHyperion = HyperionRules();
  g.mut = g_mut;
  g.bObserve = Watching();
  g.nWatchMul = g.bObserve && g_nSpeed == 6 ? g_nWatchMul : 1;
  const bool bTeamColours = g.bColours && g_nColourBy == 1 && HasTeams();
  g.seats.resize(static_cast<size_t>(nComputers) + 1);
  for (size_t i = 0; i < g_slots.size(); ++i) {
    const int nRow = SeatRow(i);
    if (nRow < 0)
      continue;
    SkirmishSeat &s = g.seats[static_cast<size_t>(nRow)];
    s.nRace = g_slots[i].nRace;
    s.nColour = g.bColours && g_slots[i].nColour != COLOUR_ANY ? g_slots[i].nColour : -1;
    if (HasTeams())
      s.nTeam = g_slots[i].nTeam + 1;
    if (g_slots[i].nKind == KIND_COMPUTER && !UseMapSettings())
      s.nLevel = g_slots[i].nLevel;
    s.nHandicap = g_slots[i].nHandicap;
    if (g_nStartsBy == 1 && StartsKnown() && StartsOffered())
      s.nStart = g_pic.starts[i].nSlot;
    if (bTeamColours)
      s.rgb = RowRgb(g_slots[i]);
    else if (g.bColours && g_slots[i].nColour == COLOUR_ANY)
      s.rgb = g_slots[i].any != 0 ? g_slots[i].any : 0x010101;  // its own colour; 0 would be none
  }
  return g;
}

// Start Game: the game made as Play Custom's Ok makes it (sub_455240, sub_4554A0,
// InitSinglePlayerCustom).
void StartGame(void)
{
  int nComputers = 0;
  for (size_t i = 1; i < g_slots.size(); ++i)
    nComputers += g_slots[i].nKind == KIND_COMPUTER;
  if (!UseMapSettings() && nComputers == 0) {
    g_note = PortText("A skirmish needs at least one computer.");
    return;
  }
  LISTENTRY *pMap = MapEntry();
  if (pMap == NULL) {
    g_note = PortText("That map is no longer there.");
    return;
  }
  const SkirmishGame g = SkirmishRecord(nComputers);
  char szDir[0x104];
  SStrCopy(szDir, g_map.dir.c_str(), sizeof szDir);
  const int nResult = CreateNewGame(reinterpret_cast<intptr_t>(pMap), reinterpret_cast<intptr_t>(gLocalPlayerName), 0,
                                    g.nKey, g_nSpeed, szDir);
  if (nResult != 0) {
    g_note = CreateError(nResult);
    FrontLog("skirmish: CreateNewGame %s gave %08x\n", g_map.name.c_str(), static_cast<unsigned>(nResult));
    return;
  }
  const int nMine = g.seats[0].nRace;
  if (!g.bObserve)
    ProfileSetLastRace(RaceKeep(nMine));
  else
    PortWatchBegin(g.nSpeed, g.nWatchMul);  // the game is made, then watched as its replay
  // the choices, which the session keeps (port_session.h); then the player's race in its slot,
  // or in melee the races Play Custom keeps for SinglePlayerMeleeInitGame (player, 7 computers)
  PortLaunchSkirmishChoices(g);
  const BYTE bMine = PortLaunchRaceBase(nMine);
  if (!g.bMelee) {
    slot_set_race(bMine);
  } else {
    byte_5264C3_g = 1;
    memset(byte_526547_g, RACE_NONE, 8);
    byte_526546_g = bMine;
    for (size_t r = 1; r < g.seats.size() && r <= 7; ++r)
      byte_526547_g[r - 1] = PortLaunchRaceBase(g.seats[r].nRace);
  }
  const bool bTeamColours = g.bColours && g_nColourBy == 1 && HasTeams();
  FrontLog("skirmish: %s, %s, speed %d, %d computers, race %d, %s rules, %d teams, starts %s, colours by %s%s\n",
           g_map.name.c_str(), g.type.c_str(), g_nSpeed, nComputers, nMine,
           g.bHyperion ? "Hyperion" : "classic", HasTeams() ? static_cast<int>(g_teamNames.size()) : 0,
           g_nStartsBy == 1 ? "by slot" : "random", bTeamColours ? "team" : "player",
           g.bObserve ? ", watched" : "");
  g_bPlayed = true;
  g_nResult = FRONT_SKIRMISH_STARTED;
}

// Start, the host's or Start Game.
void Start(void)
{
  if (g_pHost != NULL)
    g_pHost->pfnStart();
  else
    StartGame();
}

// Back from the map step: to the host, or home.
void BackOut(void)
{
  if (g_pHost != NULL)
    g_pHost->pfnBack();
  else
    g_nResult = FRONT_SKIRMISH_HOME;
}

// Fill Empty: a computer of a random race in each closed slot.
bool CanFillEmpty(void)
{
  bool bClosed = false;
  for (size_t i = 1; i < g_slots.size(); ++i)
    bClosed |= g_slots[i].nKind == KIND_CLOSED;
  return bClosed && !UseMapSettings();
}

// Distinct Colours: you keep yours, and each other seated row, in order, takes the one of 1.17's
// colours farthest from those already taken, by how far apart they look in the units and on
// the minimap (ColourGap).
bool CanDistinctColours(void)
{
  int nSeated = 0;
  for (size_t i = 0; i < g_slots.size(); ++i)
    nSeated += g_slots[i].nKind != KIND_CLOSED;
  return nSeated > 1 && ColoursChosen();
}

// Past 1.17's eleven, once none of them is clear of the colours taken (a game of more than
// eleven, the user 2026-10-26: the rows past 8 had no colour to choose), the 65.
void DistinctColours(void)
{
  std::vector<const Slot *> taken;
  if (g_slots[0].nColour >= 0)
    taken.push_back(&g_slots[0]);
  for (size_t i = 1; i < g_slots.size(); ++i) {
    if (g_slots[i].nKind == KIND_CLOSED)
      continue;
    int nBest = -1;
    float fBest = -1.0f;
    for (int nOf : {kColourCount, static_cast<int>(COLOUR_GAME)}) {
      if (fBest >= kColourClose)
        break;
      for (int c = 0; c < nOf; ++c) {
        float fNear = 1e9f;
        for (const Slot *t : taken)
          fNear = t->nColour == c ? -1.0f : fminf(fNear, ColourGap(c, 0, t->nColour, t->any));
        if (fNear > fBest) {
          fBest = fNear;
          nBest = c;
        }
      }
    }
    g_slots[i].nColour = nBest;
    taken.push_back(&g_slots[i]);
  }
  NoteSlots("Distinct Colours");
}

// The seated row whose colour is close to row i's, or -1.
int CloseTo(size_t i)
{
  const Slot &s = g_slots[i];
  if (s.nKind == KIND_CLOSED || s.nColour < 0 || !ColoursChosen())
    return -1;
  for (size_t k = 0; k < g_slots.size(); ++k)
    if (k != i && g_slots[k].nKind != KIND_CLOSED && g_slots[k].nColour >= 0 &&
        ColourGap(s.nColour, s.any, g_slots[k].nColour, g_slots[k].any) < kColourClose)
      return static_cast<int>(k);
  return -1;
}

int Clashes(void)
{
  int n = 0;
  for (size_t i = 0; i < g_slots.size(); ++i)
    n += CloseTo(i) >= 0;
  return n;
}

// A colour for a row that is seated again: its own, unless another seated row has it, else
// its row's first (RowColour), else the first no seated row has, 1.17's first.
int FreeColour(size_t nRow)
{
  for (int c = -2; c < COLOUR_GAME; ++c) {
    const int nTry = c == -2 ? g_slots[nRow].nColour : c == -1 ? RowColour(nRow) : c;
    bool bUsed = nTry < 0;
    for (size_t k = 0; k < g_slots.size() && !bUsed; ++k)
      bUsed = k != nRow && g_slots[k].nKind != KIND_CLOSED && g_slots[k].nColour == nTry;
    if (!bUsed)
      return nTry;
  }
  return -1;
}

void FillEmpty(void)
{
  for (size_t i = 1; i < g_slots.size(); ++i)
    if (g_slots[i].nKind == KIND_CLOSED) {
      g_slots[i] = Slot{KIND_COMPUTER, RACE_RANDOM, g_slots[i].nColour, g_slots[i].nTeam};
      g_slots[i].nColour = FreeColour(i);
    }
  TeamsChanged();
  NoteSlots("Fill Empty");
}

// The set-up's marks on the map's starts: by slot, a start shows its player's number, and
// with teams its team's colour (the row's own with colours by team); none otherwise.
const std::vector<MapMark> *Marks(void)
{
  static std::vector<MapMark> s_marks;
  if (!StartsKnown() || !StartsOffered() || g_nStartsBy != 1)
    return NULL;
  s_marks.assign(g_slots.size(), MapMark());
  for (size_t i = 0; i < g_slots.size(); ++i) {
    const Slot &s = g_slots[i];
    if (g_nStartsBy == 1 && s.nKind != KIND_CLOSED)
      s_marks[i].nNum = static_cast<int>(i + 1);
    if (HasTeams() && s.nKind != KIND_CLOSED) {
      s_marks[i].bRgb = true;
      s_marks[i].rgb = g_nColourBy == 1 && s.rgb != 0 ? s.rgb : TeamBase(s.nTeam);
    }
  }
  return &s_marks;
}

// The picture of pMap (NULL: none), fW by fH in SC2's units; its start marks float at nZ,
// above the box that holds it.
void Preview(const char *pszId, const MapInfo *pMap, float fW, float fH, int nZ, bool bBig = false)
{
  if (pMap == NULL)
    MapPreview(pszId, NULL, -1, NULL, 0, 0, fW, fH, nZ, bBig, false);
  else
    MapPreview(pszId, g_pic.of == pMap->name ? &g_pic : NULL, pMap->tileset, pMap->title.c_str(), pMap->w,
               pMap->h, fW, fH, nZ, bBig, UseMapSettings(), Marks());
}

// ---------------------------------------------------------------------------
// The map browser (1.17's Select Scenario list, lobby.js browser): the screen's first step,
// and Change Map's dialog over the set-up.

const float kCols[] = {90, 120, 160, 110};  // players, size, tileset, rules

// A folder's mark before its name, the way up's an arrow (lobby.css .lb-br-row.dir).
void DrawFolderMark(float x, float y, float w, float h, void *pCtx)
{
  const UiColor c = kScInkLabel;
  if (static_cast<const ScDraw *>(pCtx)->n == 1) {
    UiFill(x + w * 0.38f, y + h * 0.45f, w * 0.24f, h * 0.55f, c);
    UiLine(x + w * 0.5f, y, x + w * 0.08f, y + h * 0.55f, c, Sc(2.5f));
    UiLine(x + w * 0.5f, y, x + w * 0.92f, y + h * 0.55f, c, Sc(2.5f));
    return;
  }
  UiFill(x, y, w * 0.45f, h * 0.3f, c, Sc(1));
  UiFill(x, y + h * 0.18f, w, h * 0.82f, c, Sc(1));
}

std::vector<int> ShownRows(void)
{
  std::vector<int> shown;
  for (size_t i = 0; i < g_rows.size(); ++i)
    if (Passes(g_rows[i]))
      shown.push_back(static_cast<int>(i));
  return shown;
}

// The chosen row: a folder opens, a map leads to the set-up.
void ChooseRow(void)
{
  if (g_nPick < 0 || g_nPick >= static_cast<int>(g_rows.size()))
    return;
  const Row &r = g_rows[g_nPick];
  if (IsFolder(r))
    OpenFolder(r.p->szName);
  else if (IsMap(r))
    SetUp(g_nPick);
}

void UpFolder(void)
{
  if (lstrcmpiA(g_szDir, "maps") == 0)
    return;
  char szUp[0x104];
  SStrCopy(szUp, g_szDir, sizeof szUp);
  if (char *p = strrchr(szUp, '\\'))
    *p = 0;
  OpenFolder(szUp);
}

void MapList(void)
{
  Decl well = Box("sklist");
  well.d.layout.layoutDirection = CLAY_TOP_TO_BOTTOM;
  well.d.layout.sizing.width = Grow();
  well.d.layout.sizing.height = Grow();
  well.d.clip.vertical = true;
  Well(well);
  Open(well);

  Decl head;
  head.d.layout.sizing.width = Grow();
  head.d.layout.sizing.height = Fixed(Sc(36));
  head.d.layout.padding = Pad(Fs(16), Fs(16), 0, 0);
  head.d.layout.childGap = Gap(Fs(10));
  head.d.layout.childAlignment.y = CLAY_ALIGN_Y_CENTER;
  head.d.backgroundColor = Col(kScMenuFill);
  Open(head);
  Decl hm;
  hm.d.layout.sizing.width = Grow();
  Open(hm);
  Text(PortText("MAP"), UI_FACE_WIDE, Fs(12), 0.05f, kScInkHead);
  Close();
  const char *const kHeads[] = {PortText("PLAYERS"), PortText("SIZE"), PortText("TILESET"), PortText("RULES")};
  for (int c = 0; c < 4; ++c) {
    Decl h;
    h.d.layout.sizing.width = Fixed(Sc(kCols[c]));
    Open(h);
    Text(kHeads[c], UI_FACE_WIDE, Fs(12), 0.05f, kScInkHead);
    Close();
  }
  Close();

  // the rows that pass the filter, from the first shown
  const std::vector<int> shown = ShownRows();
  const int nMaxFirst = static_cast<int>(shown.size()) > g_nShown ? static_cast<int>(shown.size()) - g_nShown : 0;
  g_nFirst += WheelRows(Over(Id("sklist")));
  if (g_bReveal) {
    g_bReveal = false;
    for (size_t k = 0; k < shown.size(); ++k)
      if (shown[k] == g_nPick && (static_cast<int>(k) < g_nFirst || static_cast<int>(k) >= g_nFirst + g_nShown))
        g_nFirst = static_cast<int>(k) - g_nShown / 2;
  }
  g_nFirst = g_nFirst < 0 ? 0 : g_nFirst > nMaxFirst ? nMaxFirst : g_nFirst;

  // the rows in view are read, a few a frame
  int nRead = 0;
  for (int k = g_nFirst; k < static_cast<int>(shown.size()) && k < g_nFirst + g_nShown && nRead < 3; ++k) {
    Row &r = g_rows[shown[k]];
    if (IsMap(r) && !r.p->dwLoaded) {
      ReadRow(r);
      ++nRead;
    }
  }

  const float fRow = Sc(56);
  for (int k = g_nFirst; k < static_cast<int>(shown.size()) && k < g_nFirst + g_nShown; ++k) {
    const int i = shown[k];
    const Row &r = g_rows[i];
    Decl row = Box("skrow", i);
    const bool bPick = i == g_nPick;
    const bool bHot = Over(row.d.id);
    if (Clicked(row.d.id)) {
      g_nPick = i;
      g_note.clear();
      if (DoubleClicked(row.d.id)) {
        // the rows change under the loop: the list is drawn again next frame, the well
        // closed here (the row is not open yet)
        ChooseRow();
        Close();
        return;
      }
    }
    row.d.layout.sizing.width = Grow();
    row.d.layout.sizing.height = Fixed(fRow);
    row.d.layout.padding = Pad(Fs(16), Fs(16), 0, 0);
    row.d.layout.childGap = Gap(Fs(10));
    row.d.layout.childAlignment.y = CLAY_ALIGN_Y_CENTER;
    row.d.border = Border(bPick ? kScSelLine2 : kLbDivider, bPick ? Fs(3) : 0, 0, 0, 1);
    if (bPick)
      row.d.backgroundColor = Col(kLbItemHi);
    else if (bHot)
      row.d.backgroundColor = Col(kLbItem);
    Open(row);
    Decl name;
    name.d.layout.layoutDirection = CLAY_TOP_TO_BOTTOM;
    name.d.layout.sizing.width = Grow();
    name.d.layout.childGap = Gap(1);
    Open(name);
    Decl line;
    line.d.layout.childGap = Gap(Fs(10));
    line.d.layout.childAlignment.y = CLAY_ALIGN_Y_CENTER;
    Open(line);
    if (IsFolder(r)) {
      Decl mark;
      mark.d.layout.sizing.width = Fixed(Sc(18));
      mark.d.layout.sizing.height = Fixed(Sc(13));
      mark.d.custom.customData = Custom(DrawFolderMark, kScButton, 0.0f, IsUp(r) ? 1 : 0);
      Open(mark);
      Close();
    }
    Text(IsUp(r) ? PortText("Up One Level") : Keep(Title(r)), UI_FACE_BOLD, Fs(18), 0.0f,
         bPick || bHot ? kScInkHi : kLbInkInfo);
    Close();
    if (IsMap(r))
      Text(Keep(r.text), UI_FACE_TEXT, Fs(13), 0.0f, kScInkOff);
    else if (IsUp(r))
      // {0} the folder it goes to
      Text(Keep(PortTextF("To {0} \xc2\xb7 Backspace", {PathLabel(r.p->szName)})), UI_FACE_TEXT, Fs(13), 0.0f, kScInkOff);
    else
      Text(PortText("Folder"), UI_FACE_TEXT, Fs(13), 0.0f, kScInkOff);
    Close();
    if (IsFolder(r)) {
      // the folder's count across the other columns
      Decl cnt;
      cnt.d.layout.sizing.width = Fixed(Sc(kCols[0] + kCols[1] + kCols[2] + kCols[3] + 30));
      Open(cnt);
      if (!IsUp(r)) {
        const int n = i < static_cast<int>(g_folderMaps.size()) ? g_folderMaps[i].n : 0;
        Text(Keep(n ? PortTextNF("{0} map", "{0} maps", n, {std::to_string(n)}) : std::string(PortText("No maps"))),
             UI_FACE_TEXT, Fs(16), 0.0f, n ? kLbInkDesc : kScInkOff);
      }
      Close();
    } else {
      std::string cells[4];
      bool bBig = false;
      if (IsMap(r) && r.p->dwLoaded && r.p->dwLoading == 0) {
        cells[0] = std::to_string(Players(r));
        cells[1] = std::to_string(r.p->wWidth) + " \xc3\x97 " + std::to_string(r.p->wHeight);
        cells[2] = TilesetName(r);
        bBig = BigMap(Players(r), r.p->wWidth, r.p->wHeight);
        cells[3] = bBig ? "Hyperion" : PortText("Either");
      } else if (IsMap(r) && r.p->dwLoaded) {
        cells[0] = r.p->dwLoading == 2 ? PortText("Too many") : PortText("Unreadable");
      }
      for (int c = 0; c < 4; ++c) {
        Decl cell;
        cell.d.layout.sizing.width = Fixed(Sc(kCols[c]));
        Open(cell);
        Text(Keep(cells[c]), UI_FACE_TEXT, Fs(16), 0.0f, c < 3 ? kLbInkDesc : bBig ? kScInkAccept : kScInkButton);
        Close();
      }
    }
    Close();
  }
  if (shown.empty() || (shown.size() == 1 && IsUp(g_rows[shown[0]]))) {
    Decl none;
    none.d.layout.padding = Pad(Fs(30), Fs(30), Fs(30), Fs(30));
    Open(none);
    Text(g_hereMaps.n == 0 ? PortText("No maps in this folder.")
                           : PortText("No map in this folder has that many players at that size."),
         UI_FACE_TEXT, Fs(17), 0.0f, kScInkOff);
    Close();
  }
  Close();

  // how many rows fit, for the next frame
  Clay_ElementData data = Clay_GetElementData(Id("sklist"));
  if (data.found) {
    const int n = static_cast<int>((data.boundingBox.height - Sc(40)) / fRow);
    g_nShown = n > 1 ? n : 1;
  }
}

// The side: the picked map, or the picked folder (or the one open) and what it holds.
void BrowserSide(void)
{
  Decl side;
  side.d.layout.layoutDirection = CLAY_TOP_TO_BOTTOM;
  side.d.layout.sizing.width = Fixed(Sc(500));
  side.d.layout.sizing.height = Grow();
  side.d.layout.childGap = Gap(Fs(4));
  side.d.clip.vertical = true;
  Open(side);
  Row *pPick = g_nPick >= 0 && g_nPick < static_cast<int>(g_rows.size()) ? &g_rows[g_nPick] : NULL;
  if (pPick != NULL && IsMap(*pPick) && pPick->p->dwLoaded) {
    ReadStarts(pPick->p->szName);
    const MapInfo m = InfoOf(*pPick);
    Preview("skbrpreview", &m, 500, 480, g_bBrowse ? 75 : 30);
    Decl gap;
    gap.d.layout.sizing.height = Fixed(Sc(6));
    Open(gap);
    Close();
    Text(Keep(m.title), UI_FACE_BOLD, Fs(24), 0.0f, kLbInkInfo);
    if (pPick->p->dwLoading == 0) {
      // {3} the map's tileset
      Text(Keep(PortTextF("{0} starts \xc2\xb7 {1} \xc3\x97 {2} tiles \xc2\xb7 {3}",
                          {std::to_string(m.players), std::to_string(m.w), std::to_string(m.h),
                           PortText(kTilesets[m.tileset])})),
           UI_FACE_TEXT, Fs(16), 0.0f, kLbInkDesc);
      Text(Keep(PathLabel(g_szDir) + m.file), UI_FACE_TEXT, Fs(14), 0.0f, kScInkOff);
      Text(MapKind(m.file), UI_FACE_TEXT, Fs(14), 0.0f, kScInkOff);
      if (BigMap(m.players, m.w, m.h))
        Text(m.players > 8 ? PortText("Plays under Hyperion rules: past the original\xe2\x80\x99s 8 players.")
                           : PortText("Plays under Hyperion rules: past the original\xe2\x80\x99s 256 tiles."),
             UI_FACE_TEXT, Fs(14), 0.0f, kScInkAccept);
      else
        Text(PortText("Classic or Hyperion rules."), UI_FACE_TEXT, Fs(14), 0.0f, kScInkOff);
      if (!m.desc.empty())
        Text(Keep(m.desc), UI_FACE_TEXT, Fs(15), 0.0f, kScInkButton, -1, 1.35f);
    } else {
      Text(pPick->p->dwLoading == 2 ? PortText("That map has more players than Hyperion holds.")
                                    : PortText("That map could not be read."),
           UI_FACE_TEXT, Fs(16), 0.0f, kScInkAccept);
    }
  } else if (pPick != NULL && IsMap(*pPick)) {
    Preview("skbrpreview", NULL, 500, 480, 30);
  } else {
    // a folder: the one picked, else the one open
    const bool bFolder = pPick != NULL && IsFolder(*pPick) && !IsUp(*pPick);
    const FolderMaps &fm = bFolder ? g_folderMaps[g_nPick] : g_hereMaps;
    const std::string dir = bFolder ? std::string(pPick->p->szName) : std::string(g_szDir);
    const size_t nSlash = dir.rfind('\\');
    const std::string name = dir.size() <= 4 ? std::string("Maps") : dir.substr(nSlash == std::string::npos ? 0 : nSlash + 1);
    Text(Keep(name), UI_FACE_BOLD, Fs(24), 0.0f, kLbInkInfo);
    Text(Keep(PathLabel(dir.c_str())), UI_FACE_TEXT, Fs(16), 0.0f, kLbInkDesc);
    Text(Keep(PortTextNF("{0} map, counting its folders", "{0} maps, counting its folders", fm.n,
                         {std::to_string(fm.n)})),
         UI_FACE_TEXT, Fs(14), 0.0f, kScInkOff);
    Decl names;
    names.d.layout.layoutDirection = CLAY_TOP_TO_BOTTOM;
    names.d.layout.childGap = Gap(Fs(4));
    names.d.layout.padding = Pad(0, 0, Fs(6), Fs(6));
    Open(names);
    for (size_t k = 0; k < fm.names.size() && k < 8; ++k)
      Text(Keep(fm.names[k]), UI_FACE_TEXT, Fs(16), 0.0f, kLbInkDesc);
    if (fm.names.size() > 8)
      Text(Keep(PortTextF("and {0} more", {std::to_string(fm.names.size() - 8)})), UI_FACE_TEXT, Fs(16), 0.0f,
           kScInkOff);
    Close();
    if (bFolder)
      Text(PortText("Double-click or Enter opens it."), UI_FACE_TEXT, Fs(14), 0.0f, kScInkOff);
  }
  Close();
}

void Browser(void)
{
  Decl title;
  title.d.layout.sizing.width = Grow();
  title.d.layout.childAlignment.x = CLAY_ALIGN_X_CENTER;
  Open(title);
  Text(PortText("CHOOSE A MAP"), UI_FACE_WIDE, Fs(28), 0.04f, kScInkHi);
  Close();

  Decl filters;
  filters.d.layout.sizing.width = Grow();
  filters.d.layout.childGap = Gap(Fs(10));
  filters.d.layout.childAlignment.y = CLAY_ALIGN_Y_CENTER;
  filters.d.layout.padding = Pad(0, 0, Fs(6), Fs(4));
  Open(filters);
  Label(PortText("Players"), 16, kScInkLabel, UI_FACE_BOLD);
  const char *apFilters[6];
  for (int i = 0; i < 6; ++i)
    apFilters[i] = PortText(kFilters[i]);
  const int nF = Segments("skfilter", apFilters, 6, g_nFilter);
  if (nF >= 0) {
    g_nFilter = nF;
    g_nFirst = 0;
  }
  Decl gap;
  gap.d.layout.sizing.width = Fixed(Sc(10));
  Open(gap);
  Close();
  Label(PortText("Size"), 16, kScInkLabel, UI_FACE_BOLD);
  const char *apSizes[4];
  for (int i = 0; i < 4; ++i)
    apSizes[i] = PortText(kSizes[i]);
  const int nS = Segments("sksize", apSizes, 4, g_nSize);
  if (nS >= 0) {
    g_nSize = nS;
    g_nFirst = 0;
  }
  Spacer();
  Label(gbIsExpansion ? PortText("Maps for Brood War") : PortText("Maps for StarCraft"), 16, kLbInkDesc);
  Close();

  Text(Keep(PathLabel(g_szDir)), UI_FACE_BOLD, Fs(18), 0.0f, kLbInkInfo);

  Decl body;
  body.d.layout.sizing.width = Grow();
  body.d.layout.sizing.height = Grow();
  body.d.layout.childGap = Gap(Fs(24));
  Open(body);
  MapList();
  BrowserSide();
  Close();
}

void MapStep(void)
{
  if (g_pHost != NULL && g_pHost->pfnHead != NULL) {
    Decl hb = Floating("skhostpick", 0, -Sc(34), Sc(32), 21, CLAY_ATTACH_POINT_RIGHT_TOP, CLAY_ATTACH_POINT_RIGHT_TOP);
    hb.d.layout.childGap = Gap(Fs(8));
    Open(hb);
    g_pHost->pfnHead();
    Close();
  }
  Browser();
  Decl foot;
  foot.d.layout.sizing.width = Grow();
  foot.d.layout.childGap = Gap(Fs(6));
  foot.d.layout.childAlignment.y = CLAY_ALIGN_Y_CENTER;
  foot.d.layout.padding = Pad(0, 0, Fs(4), 0);
  Open(foot);
  const Row *pPick = g_nPick >= 0 && g_nPick < static_cast<int>(g_rows.size()) ? &g_rows[g_nPick] : NULL;
  Decl choose = Box("skchoose");
  choose.d.layout.sizing.width = Fixed(Sc(250));
  choose.d.layout.sizing.height = Fixed(Sc(52));
  const bool bChoose = ScButton(choose, pPick != NULL && IsFolder(*pPick) ? PortText("Open Folder") : PortText("Choose Map"),
                                'O', true, pPick != NULL && (IsMap(*pPick) || IsFolder(*pPick)));
  Decl back = Box("skback");
  back.d.layout.sizing.width = Fixed(Sc(250));
  back.d.layout.sizing.height = Fixed(Sc(52));
  const bool bBack = ScButton(back, PortText("Back"), 'B', false);
  Decl hint;
  hint.d.layout.padding = Pad(Fs(18), 0, 0, 0);
  Open(hint);
  if (!g_note.empty())
    Label(g_note.c_str(), 16, kScInkAccept);
  else if (!g_map.name.empty() && !g_slots.empty())
    // {0} the map's name
    Label(Keep(PortTextF("The set-up waits: choose {0} again to keep it.", {g_map.title})), 16, kLbInkDesc);
  else
    Label(g_pHost != NULL ? PortText(g_pHost->pszPickHint) : PortText("Slots, races and options come next."), 16,
          kLbInkDesc);
  Close();
  Close();

  if (bChoose)
    ChooseRow();
  else if (bBack)
    BackOut();
}

// Keys for the browser, the step's or the dialog's; true when it took one.
bool BrowserKeys(bool bDialog)
{
  const std::vector<int> shown = ShownRows();
  int k = -1;
  for (size_t j = 0; j < shown.size(); ++j)
    if (shown[j] == g_nPick)
      k = static_cast<int>(j);
  if (Key(VK_DOWN) && k + 1 < static_cast<int>(shown.size()))
    k = k + 1;
  else if (Key(VK_UP) && k > 0)
    k = k - 1;
  else if (Key(VK_BACK)) {
    UpFolder();
    return true;
  } else if (Key(VK_RETURN) || Key('O')) {
    ChooseRow();
    return true;
  } else if (bDialog && (Key(VK_ESCAPE) || Key('C'))) {
    g_bBrowse = false;
    g_note.clear();
    return true;
  } else if (!bDialog && (Key(VK_ESCAPE) || Key('B'))) {
    BackOut();
    return true;
  }
  if (k >= 0 && k < static_cast<int>(shown.size()) && shown[k] != g_nPick) {
    g_nPick = shown[k];
    if (k < g_nFirst)
      g_nFirst = k;
    else if (k >= g_nFirst + g_nShown)
      g_nFirst = k - g_nShown + 1;
  }
  return true;
}

void ChangeMapDialog(void)
{
  const bool bOut = DialogOpen("skbrowse", 1500, fminf(960.0f, UiHeight() / Sc(1) - 140), 100);
  Browser();
  Decl row;
  row.d.layout.sizing.width = Grow();
  row.d.layout.childGap = Gap(Fs(12));
  row.d.layout.childAlignment.x = CLAY_ALIGN_X_CENTER;
  row.d.layout.childAlignment.y = CLAY_ALIGN_Y_CENTER;
  row.d.layout.padding = Pad(0, 0, Fs(6), 0);
  Open(row);
  const Row *pPick = g_nPick >= 0 && g_nPick < static_cast<int>(g_rows.size()) ? &g_rows[g_nPick] : NULL;
  Decl choose = Box("skbchoose");
  choose.d.layout.sizing.width = Fixed(Sc(250));
  choose.d.layout.sizing.height = Fixed(Sc(52));
  const bool bChoose = ScButton(choose, pPick != NULL && IsFolder(*pPick) ? PortText("Open Folder") : PortText("Choose"),
                                'O', true, pPick != NULL && (IsMap(*pPick) || IsFolder(*pPick)));
  Decl cancel = Box("skbcancel");
  cancel.d.layout.sizing.width = Fixed(Sc(250));
  cancel.d.layout.sizing.height = Fixed(Sc(52));
  const bool bCancel = ScButton(cancel, PortText("Cancel"), 'C', false);
  if (!g_note.empty())
    Label(g_note.c_str(), 16, kScInkAccept);
  Close();
  Close();  // the dialog
  if (bChoose)
    ChooseRow();
  else if (bCancel || bOut) {
    g_bBrowse = false;
    g_note.clear();
  }
}

// ---------------------------------------------------------------------------
// The set-up (lobby.js fill): the header, the slots, the game options, the game info, the
// mutators and the foot, each where SC2's battle lobby puts it.

// The races a slot offers: the three, a mod's (for the player, and for the first seven
// computers, which Play Custom seats one by one), then Random.
std::vector<std::string> RaceNames(bool bMine, bool bComputers, std::vector<int> *pValues)
{
  std::vector<std::string> names = {PortText("Zerg"), PortText("Terran"), PortText("Protoss")};
  *pValues = {RACE_ZERG, RACE_TERRAN, RACE_PROTOSS};
  if (ModDataId()[0] != '\0' && (bMine || bComputers))
    for (int i = 1; i <= ModRaceCount(); ++i) {
      names.push_back(ModRaceName(i));
      pValues->push_back(RACE_MOD + i);
    }
  names.push_back(PortText("Random"));
  pValues->push_back(RACE_RANDOM);
  return names;
}

bool Compact(void)
{
  return g_nDensity == 2 || (g_nDensity == 0 && g_slots.size() > 8);
}

// The fold (lobby.css .lb-fold): a chevron, down when open.
void DrawFold(float x, float y, float w, float h, void *pCtx)
{
  const ScDraw &d = *static_cast<const ScDraw *>(pCtx);
  const UiColor c = (d.n & 2) ? kScInkHi : kScInkButton;
  const float cx = x + w / 2, cy = y + h / 2, s = Sc(5);
  if (d.n & 1) {
    UiLine(cx - s, cy - s / 2, cx, cy + s / 2, c, Sc(2));
    UiLine(cx, cy + s / 2, cx + s, cy - s / 2, c, Sc(2));
  } else {
    UiLine(cx - s / 2, cy - s, cx + s / 2, cy, c, Sc(2));
    UiLine(cx + s / 2, cy, cx - s / 2, cy + s, c, Sc(2));
  }
}

// The colour picker (teams.js picker): the game's 65 round the hue wheel, or any colour; its
// tab, the colour being picked on Any colour, and the row it was opened for.
int g_nColourTab;
unsigned g_anyDraft;
std::string g_colourFor;

// The other seated rows that choose colours, which a colour is held against.
std::vector<int> OtherRows(int i)
{
  std::vector<int> v;
  for (size_t k = 0; k < g_slots.size(); ++k)
    if (static_cast<int>(k) != i && g_slots[k].nKind != KIND_CLOSED && g_slots[k].nColour >= 0)
      v.push_back(static_cast<int>(k));
  return v;
}

// The other row whose colour is nearest colour c (rgb with COLOUR_ANY), and how near: -1 with none.
int NearestRow(const std::vector<int> &others, int c, unsigned rgb, float *pfGap)
{
  int nWho = -1;
  *pfGap = 9.0f;
  for (int k : others) {
    const float d = ColourGap(c, rgb, g_slots[k].nColour, g_slots[k].any);
    if (d < *pfGap) {
      *pfGap = d;
      nWho = k;
    }
  }
  return nWho;
}

// Row i takes colour c (rgb with COLOUR_ANY); with nSwap, that row takes row i's.
void TakeColour(int i, int c, unsigned rgb, int nSwap)
{
  Slot &s = g_slots[i];
  if (nSwap >= 0) {
    g_slots[nSwap].nColour = s.nColour;
    g_slots[nSwap].any = s.any;
  }
  s.nColour = c;
  s.any = c == COLOUR_ANY ? rgb : 0;
  g_lbOpen.clear();
  NoteSlots("a colour");
}

// The game's colours: a cell each, with the number of the player who has one and a warning rim
// on one close to a player's; a taken one, or one that reads as another player's, swaps. Under
// them, the colour pointed at and what picking it does, and a key to the numbers and the rims
// shown only while a cell has one (the user, 2026-10-26: the rim's line stayed when no cell had
// a rim).
void GameColours(int i, const std::vector<int> &others)
{
  const Slot &s = g_slots[i];
  const std::vector<int> &order = ColourOrder();
  const int kAcross = 13;
  std::string pointed;
  int nPick = -1, nSwap = -1;
  bool bAnyOwned = false, bAnyRim = false;
  Decl grid;
  grid.d.layout.layoutDirection = CLAY_TOP_TO_BOTTOM;
  grid.d.layout.childGap = Gap(Fs(4));
  Open(grid);
  for (size_t r0 = 0; r0 < order.size(); r0 += kAcross) {
    Decl row;
    row.d.layout.childGap = Gap(Fs(4));
    Open(row);
    for (size_t n = r0; n < order.size() && n < r0 + kAcross; ++n) {
      const int c = order[n];
      int nOwner = -1;
      for (int k : others)
        if (g_slots[k].nColour == c)
          nOwner = k;
      float fGap = 9.0f;
      const int nWho = NearestRow(others, c, 0, &fGap);
      Decl cell = Box("skcell", c);
      const bool bCellHot = Over(cell.d.id);
      if (bCellHot) {
        // {0} the colour's name, {1} the player's number, {2} what the two colours share
        const std::string who = std::to_string((nOwner >= 0 ? nOwner : nWho) + 1);
        pointed = ColourName(c);
        if (nOwner >= 0)
          pointed = PortTextF("{0}: player {1}\xe2\x80\x99s colour. Picking it swaps the two.", {pointed, who});
        else if (fGap < kColourSame)
          pointed = PortTextF("{0}: reads as player {1}\xe2\x80\x99s colour {2}. Picking it swaps the two.",
                              {pointed, who, ColourCloseOn(c, 0, g_slots[nWho].nColour, g_slots[nWho].any)});
        else if (fGap < kColourClose)
          pointed = PortTextF("{0}: hard to tell from player {1}\xe2\x80\x99s colour {2}.",
                              {pointed, who, ColourCloseOn(c, 0, g_slots[nWho].nColour, g_slots[nWho].any)});
      }
      if (Clicked(cell.d.id)) {
        nPick = c;
        nSwap = nOwner >= 0 ? nOwner : fGap < kColourSame ? nWho : -1;
      }
      cell.d.layout.sizing.width = Fixed(Sc(32));
      cell.d.layout.sizing.height = Fixed(Sc(32));
      cell.d.layout.childAlignment.x = CLAY_ALIGN_X_CENTER;
      cell.d.layout.childAlignment.y = CLAY_ALIGN_Y_CENTER;
      cell.d.backgroundColor = Col(UiRgb(ColourRgb(c)));
      Radius(cell, 4);
      const bool bCur = c == s.nColour;
      const bool bRim = nOwner < 0 && fGap < kColourClose;
      bAnyOwned |= nOwner >= 0;
      bAnyRim |= bRim && !bCur;
      cell.d.border = bCur || bCellHot ? Border(kScInkHi, Fs(2), Fs(2), Fs(2), Fs(2))
                      : bRim           ? Border(kLbWarn, Fs(2), Fs(2), Fs(2), Fs(2))
                                       : Border(kScBlack, 1, 1, 1, 1);
      Open(cell);
      if (nOwner >= 0)
        Text(Keep(std::to_string(nOwner + 1)), UI_FACE_BOLD, Fs(13), 0.0f, kScInkHi);
      Close();
    }
    Close();
  }
  Close();
  if (nPick >= 0) {
    TakeColour(i, nPick, 0, nSwap);
    return;
  }
  if (pointed.empty())
    pointed = s.nColour == COLOUR_ANY ? PortTextF("Now its own colour, {0}", {ColourWord(s)})
                                      : PortTextF("Now {0}", {ColourWord(s)});
  Text(Keep(pointed), UI_FACE_TEXT, Fs(14), 0.0f, kScInkTipHead);
  Decl key;
  key.d.layout.layoutDirection = CLAY_TOP_TO_BOTTOM;
  key.d.layout.childGap = Gap(Fs(3));
  Open(key);
  if (bAnyOwned)
    Text(PortText("A number: that player\xe2\x80\x99s colour. Picking it swaps the two."), UI_FACE_TEXT, Fs(14), 0.0f,
         kScInkTip);
  if (bAnyRim)
    Text(PortText("Orange rim: hard to tell from a player\xe2\x80\x99s colour."), UI_FACE_TEXT, Fs(14), 0.0f, kLbWarn);
  Close();
}

void Shade(unsigned rgb, float fSide)
{
  Decl sh;
  sh.d.layout.sizing.width = Fixed(Sc(fSide));
  sh.d.layout.sizing.height = Fixed(Sc(fSide));
  sh.d.backgroundColor = Col(UiRgb(rgb));
  sh.d.border = Border(kScBlack, 1, 1, 1, 1);
  Open(sh);
  Close();
}

// Any colour: the field and hue strip, hex and R G B, and what the game draws from it, the
// units' eight shades and the minimap's dot; one that reads as another player's cannot be used.
void AnyColour(int i, const std::vector<int> &others)
{
  Decl row;
  row.d.layout.childGap = Gap(Fs(14));
  Open(row);
  Decl left;
  left.d.layout.layoutDirection = CLAY_TOP_TO_BOTTOM;
  left.d.layout.sizing.width = Fixed(Sc(250));
  left.d.layout.childGap = Gap(Fs(8));
  Open(left);
  ColourAnyField(&g_anyDraft, true);
  ColourAnyValues(&g_anyDraft, true);
  Close();
  Decl side;
  side.d.layout.layoutDirection = CLAY_TOP_TO_BOTTOM;
  side.d.layout.sizing.width = Fixed(Sc(200));
  side.d.layout.sizing.height = Grow();
  side.d.layout.childGap = Gap(Fs(6));
  Open(side);
  const ColourLook &look = ColourLookOf(COLOUR_ANY, g_anyDraft);
  Text(PortText("The units"), UI_FACE_BOLD, Fs(13), 0.0f, kScInkLabel);
  Decl ramp;
  ramp.d.layout.childGap = Gap(Fs(2));
  Open(ramp);
  for (int k = 0; k < 8; ++k)
    Shade(look.anShade[k], 22);
  Close();
  Text(PortText("The minimap"), UI_FACE_BOLD, Fs(13), 0.0f, kScInkLabel);
  Shade(look.nMinimap, 22);
  float fGap = 9.0f;
  const int nWho = NearestRow(others, COLOUR_ANY, g_anyDraft, &fGap);
  const bool bSame = nWho >= 0 && fGap < kColourSame;
  std::string verdict = others.empty() ? PortText("Nobody else to compare with.")
                                       : PortText("Clear of every other player, on the minimap and in the units.");
  if (nWho >= 0 && fGap < kColourClose) {
    // {0} the player's number, {1} what the two colours share
    const std::string who = std::to_string(nWho + 1);
    const std::string on = ColourCloseOn(COLOUR_ANY, g_anyDraft, g_slots[nWho].nColour, g_slots[nWho].any);
    verdict = bSame ? PortTextF("Reads as player {0}\xe2\x80\x99s colour {1}. Choose another, or give player {0} a new "
                                "colour first.",
                                {who, on})
                    : PortTextF("Close to player {0}\xe2\x80\x99s colour {1}. Allowed, and marked on both.", {who, on});
  }
  Decl words;
  words.d.layout.sizing.width = Grow();
  Open(words);
  Text(Keep(verdict), UI_FACE_TEXT, Fs(13), 0.0f, nWho >= 0 && fGap < kColourClose ? kLbWarn : kScInkTip, -1, 1.35f);
  Close();
  Spacer();
  Decl use = Box("skcoluse");
  use.d.layout.sizing.width = Grow();
  use.d.layout.sizing.height = Fixed(Sc(34));
  if ((ScMini(use, PortText("Use This Colour"), 'U', !bSame) || (!bSame && !ColourAnyEntryFocused() && Key('U'))))
    TakeColour(i, COLOUR_ANY, g_anyDraft != 0 ? g_anyDraft : 0x010101, -1);
  Close();
  Close();
}

// The colour tile (lobby.css .lb-swatch): the colour and its name; a click opens the
// picker. One close to another player's is rimmed in warning and marked.
void Swatch(int i)
{
  Slot &s = g_slots[i];
  Decl b = Box("skswatch", i);
  const bool bHot = Over(b.d.id);
  const std::string key = "colour#" + std::to_string(i);
  if (Clicked(b.d.id))
    g_lbOpen = g_lbOpen == key ? std::string() : key;
  // the picker opened: on the tab of the row's colour, Any colour from the row's
  if (g_lbOpen == key && g_colourFor != key) {
    g_colourFor = key;
    g_nColourTab = s.nColour == COLOUR_ANY ? 1 : 0;
    g_anyDraft = Chosen(s);
    ColourAnyReset();
  } else if (g_lbOpen != key && g_colourFor == key) {
    g_colourFor.clear();
  }
  b.d.layout.sizing.width = Fixed(Sc(kSwatchW));
  b.d.layout.sizing.height = Fixed(Sc(36));
  b.d.layout.padding = Pad(Fs(8), Fs(8), 0, 0);
  b.d.layout.childGap = Gap(Fs(8));
  b.d.layout.childAlignment.y = CLAY_ALIGN_Y_CENTER;
  const int nClose = CloseTo(static_cast<size_t>(i));
  ScLook look = bHot || g_lbOpen == key ? kScDropHi : kScDrop;
  if (nClose >= 0)
    look.rim = kLbWarn;
  b.d.custom.customData = Custom(DrawScRaised, look);
  Open(b);
  Chip(Chosen(s), 20);
  Decl nm;
  nm.d.layout.sizing.width = Grow();
  Open(nm);
  // on one line (the user, 2026-10-26: "Pale Yellow" wrapped): the padding and gaps are pixels,
  // Sc, as Pad and Gap make them
  TextFitted(ColourWord(s), 14, 12, Sc(kSwatchW) - 3 * Sc(8) - Sc(20), kScInkButton);
  Close();
  if (nClose >= 0) {
    // the mark sits on the tile's corner, so the name keeps the tile's width ("Pale Dusty Aqua");
    // above the set-up's boxes (At, 21), under an open list
    Decl mark = Floating(NULL, 0, -Sc(3), Sc(3), 22, CLAY_ATTACH_POINT_CENTER_CENTER, CLAY_ATTACH_POINT_RIGHT_TOP);
    mark.d.floating.clipTo = CLAY_CLIP_TO_ATTACHED_PARENT;
    mark.d.layout.sizing.width = Fixed(Sc(16));
    mark.d.layout.sizing.height = Fixed(Sc(16));
    mark.d.layout.childAlignment.x = CLAY_ALIGN_X_CENTER;
    mark.d.layout.childAlignment.y = CLAY_ALIGN_Y_CENTER;
    mark.d.backgroundColor = Col(kLbWarn);
    Radius(mark, 8);
    Open(mark);
    Text("!", UI_FACE_BOLD, Fs(12), 0.0f, kScBlack);
    Close();
    if (g_lbOpen != key) {
      const Slot &o = g_slots[nClose];
      TipOn(b.d.id, PortText("Close colours"),
            Keep(PortTextF("Hard to tell from player {0}\xe2\x80\x99s colour {1}. Pick another, or press Distinct "
                           "Colours to separate every player\xe2\x80\x99s.",
                           {std::to_string(nClose + 1), ColourCloseOn(s.nColour, s.any, o.nColour, o.any)})));
    }
  }
  if (g_lbOpen == key) {
    Decl pop;
    pop.d.id = Id("skpicker");
    pop.d.layout.layoutDirection = CLAY_TOP_TO_BOTTOM;
    pop.d.layout.sizing.width = Fixed(Sc(464) + 2 * Fs(14));
    pop.d.layout.padding = Pad(Fs(14), Fs(14), Fs(14), Fs(14));
    pop.d.layout.childGap = Gap(Fs(10));
    pop.d.floating.attachTo = CLAY_ATTACH_TO_PARENT;
    // under its row's swatch, or over it near the screen's foot (its height as last laid out)
    const Clay_ElementData was = Clay_GetElementData(pop.d.id);
    const Clay_ElementData face = Clay_GetElementData(b.d.id);
    const float fBelow = face.found ? UiHeight() - (face.boundingBox.y + face.boundingBox.height) : 0.0f;
    const bool bUp = face.found && was.found && was.boundingBox.height + Sc(6) > fBelow && face.boundingBox.y > fBelow;
    pop.d.floating.attachPoints.element = bUp ? CLAY_ATTACH_POINT_LEFT_BOTTOM : CLAY_ATTACH_POINT_LEFT_TOP;
    pop.d.floating.attachPoints.parent = bUp ? CLAY_ATTACH_POINT_LEFT_TOP : CLAY_ATTACH_POINT_LEFT_BOTTOM;
    pop.d.floating.offset.y = bUp ? -Sc(6) : Sc(6);
    pop.d.floating.zIndex = 90;
    const ScLook tip = {kScTipFill, kScTipFill, kScTipRim, kScTipFill, kScBlack, 4};
    pop.d.custom.customData = Custom(DrawScBox, tip);
    Clicked(pop.d.id);  // a press anywhere on it is its own, which keeps it open
    Open(pop);
    Text(Keep(PortTextF("Colour for player {0}", {std::to_string(i + 1)})), UI_FACE_WIDE, Fs(15), 0.03f, kScInkTipHead);
    static const char *const kTabs[] = {PT_("The game\xe2\x80\x99s colours"), PT_("Any colour")};
    const char *const apTabs[] = {PortText(kTabs[0]), PortText(kTabs[1])};
    const int nTab = Segments("skcoltab", apTabs, 2, g_nColourTab);
    if (nTab >= 0 && nTab != g_nColourTab) {
      g_nColourTab = nTab;
      ColourAnyReset();
    }
    const std::vector<int> others = OtherRows(i);
    if (g_nColourTab == 0)
      GameColours(i, others);
    else
      AnyColour(i, others);
    Close();
  }
  Close();
}

// The skirmish's observer slot, a row as a player's is: the player, when their own seat is a
// computer's, or empty. Its list moves the player between it and their seat.
void ObserverRow(bool bWatch)
{
  const bool bCompact = Compact();
  Open(SlotGround("skobs", 0, bCompact, bWatch));
  SlotNumber("skobsnum", 0, bCompact);
  TipOn(Id("skobsnum", 0), PortText("Observer 1"),
        PortText("An observer has no seat: they watch every seat play, the whole map shown."));
  Decl who;
  who.d.layout.sizing.width = Grow();
  who.d.layout.childGap = Gap(Fs(10));
  who.d.layout.childAlignment.y = CLAY_ALIGN_Y_CENTER;
  Open(who);
  if (!bCompact && bWatch) {
    Decl ava;
    ava.d.layout.sizing.width = Fixed(Sc(50));
    ava.d.layout.sizing.height = Fixed(Sc(50));
    ava.d.layout.childAlignment.x = CLAY_ALIGN_X_CENTER;
    ava.d.layout.childAlignment.y = CLAY_ALIGN_Y_CENTER;
    ava.d.custom.customData = Custom(DrawAva, kScButton, 0.0f, 0);
    Open(ava);
    const char c = gLocalPlayerName[0];
    Text(Keep(std::string(1, c ? static_cast<char>(toupper(static_cast<unsigned char>(c))) : '?')), UI_FACE_WIDE, Fs(20),
         0.0f, kScInkHi);
    Close();
  }
  const int nKind = Pulldown("skobskind", 0, 150, {PortText("You"), PortText("Empty")}, bWatch ? 0 : 1,
                             UseMapSettings() ? PD_OFF : PD_ON);
  if (UseMapSettings())
    TipOn(Id("skobskind", 0), PortText("Use Map Settings"),
          PortText("The map seats the players and runs the game, and expects you in your seat. Watching a game of computers "
                   "is for melee games."));
  else
    TipOn(Id("skobskind", 0), PortText("Observer"),
          PortText("Watch instead of playing: your seat becomes a computer, and you watch every seat play with the whole map "
                   "shown, the replay\xe2\x80\x99s controls and speeds up to 16 times Fastest."));
  if (nKind >= 0 && (nKind == 0) != bWatch) {
    // the player's seat: a computer's while they watch, theirs again when the slot is emptied
    Slot &s = g_slots[0];
    s.nKind = nKind == 0 ? KIND_COMPUTER : KIND_YOU;
    s.name.clear();
    s.chip.clear();
    s.bReady = false;
    NoteSlots("the observer slot");
  }
  if (bWatch) {
    if (!bCompact)
      Text(PortText("The whole map shown, the replay\xe2\x80\x99s controls, up to 16 times Fastest"), UI_FACE_TEXT,
           Fs(14), 0.0f, kLbInkDesc);
  } else {
    Decl empty;
    empty.d.layout.layoutDirection = CLAY_TOP_TO_BOTTOM;
    empty.d.layout.sizing.width = Grow();
    empty.d.layout.childAlignment.x = CLAY_ALIGN_X_CENTER;
    Open(empty);
    Text(PortText("EMPTY"), UI_FACE_WIDE, Fs(16), 0.06f, kLbInkEmpty);
    if (!bCompact)
      Text(UseMapSettings() ? PortText("watching is for melee games")
                            : PortText("choose You to watch a game of computers"),
           UI_FACE_TEXT, Fs(13), 0.0f, kLbInkDesc);
    Close();
  }
  Close();
  Close();
}

void SlotRow(int i)
{
  Slot &s = g_slots[i];
  const bool bCompact = Compact();
  const bool bSeated = Seated(s);
  const bool bMine = Mine(s);
  Open(SlotGround("skslot", i, bCompact, bSeated));
  SlotNumber("sknum", i, bCompact);
  if (g_nStartsBy == 1 && StartsOffered())
    TipOn(Id("sknum", i), Keep(PortTextF("Player {0}", {std::to_string(i + 1)})),
          Keep(PortTextF("Starts by slot: player {0} starts where the map\xe2\x80\x99s picture shows {0}.",
                         {std::to_string(i + 1)})));
  else
    TipOn(Id("sknum", i), Keep(PortTextF("Player {0}", {std::to_string(i + 1)})),
          PortText("Starts are drawn at random in a melee game, so a slot is not a place on the map. Choose Start positions: "
                   "By slot to give each player a start."));

  Decl who;
  who.d.layout.sizing.width = Grow();
  who.d.layout.childGap = Gap(Fs(10));
  who.d.layout.childAlignment.y = CLAY_ALIGN_Y_CENTER;
  Open(who);
  if (!bCompact && bSeated) {
    Decl ava;
    ava.d.layout.sizing.width = Fixed(Sc(50));
    ava.d.layout.sizing.height = Fixed(Sc(50));
    ava.d.layout.childAlignment.x = CLAY_ALIGN_X_CENTER;
    ava.d.layout.childAlignment.y = CLAY_ALIGN_Y_CENTER;
    const bool bPerson = s.nKind == KIND_YOU || s.nKind == KIND_PERSON;
    ava.d.custom.customData = Custom(DrawAva, kScButton, 0.0f, bPerson ? 0 : 1);
    Open(ava);
    const std::string name = !s.name.empty() ? s.name : std::string(gLocalPlayerName);
    if (bPerson)
      Text(Keep(std::string(1, name[0] ? static_cast<char>(toupper(static_cast<unsigned char>(name[0]))) : '?')),
           UI_FACE_WIDE, Fs(20), 0.0f, kScInkHi);
    else
      Text("AI", UI_FACE_WIDE, Fs(15), 0.0f, kScIconHi);
    Close();
  }
  // a map with its own settings expects the player in their seat
  if (i == 0 && s.nKind == KIND_COMPUTER && WatchOffered() && UseMapSettings()) {
    s.nKind = KIND_YOU;
    NoteSlots("your seat, Use Map Settings");
  }
  if (((s.nKind == KIND_YOU && RoomMode() != 1) || (s.nKind == KIND_PERSON && Guest())) && !(i == 0 && WatchOffered())) {
    // this player, or, in a guest's set-up, any of the room's people: their name, and a line
    Text(Keep(!s.name.empty() ? s.name : std::string(gLocalPlayerName)), UI_FACE_TEXT, Fs(17), 0.0f, kScInkLabel);
    Text(bMine ? (RoomMode() == 1 ? PortText("You, the host") : PortText("You")) : i == 0 ? PortText("The host") : "",
         UI_FACE_TEXT, Fs(14), 0.0f, kLbInkDesc);
  } else if (Guest()) {
    Text(s.nKind == KIND_COMPUTER ? PortText("Computer") : "", UI_FACE_TEXT, Fs(17), 0.0f, kScInkLabel);
  } else {
    // the host's kinds: a room's seat may be open to its people, or hold one of them, whom
    // another kind unseats
    std::vector<std::string> kinds = {PortText("Computer"), PortText("Closed")};
    std::vector<int> values = {KIND_COMPUTER, KIND_CLOSED};
    if (i == 0 && WatchOffered()) {
      // the player plays, or watches with every seat a computer
      kinds = {PortText("You"), PortText("Computer")};
      values = {KIND_YOU, KIND_COMPUTER};
    } else if (RoomMode() != 0) {
      kinds.insert(kinds.begin(), PortText("Open"));
      values.insert(values.begin(), KIND_OPEN);
      if (s.nKind == KIND_PERSON) {
        kinds.insert(kinds.begin(), s.name);
        values.insert(values.begin(), KIND_PERSON);
      }
      // the host sits in one seat, or in none and runs the room
      bool bHostSeated = false;
      for (const Slot &o : g_slots)
        bHostSeated = bHostSeated || o.nKind == KIND_YOU;
      if (s.nKind == KIND_YOU || (!bHostSeated && s.nKind != KIND_PERSON)) {
        // {0} the player's name
        kinds.insert(kinds.begin(),
                     PortTextF("{0} (you)", {!s.name.empty() ? s.name : std::string(gLocalPlayerName)}));
        values.insert(values.begin(), KIND_YOU);
      }
    }
    int nCur = 0;
    for (size_t k = 0; k < values.size(); ++k)
      if (values[k] == s.nKind)
        nCur = static_cast<int>(k);
    const int nKind = Pulldown("skkind", i, 150, kinds, nCur, UseMapSettings() ? PD_OFF : PD_ON);
    if (UseMapSettings() && i == 0 && WatchOffered())
      TipOn(Id("skkind", i), PortText("Use Map Settings"),
            PortText("The map seats the players and runs the game, and expects you in your seat. Watching a game of "
                     "computers is for melee games."));
    else if (UseMapSettings())
      TipOn(Id("skkind", i), PortText("Use Map Settings"), PortText("The map seats the players and runs the game."));
    else if (i == 0 && WatchOffered())
      TipOn(Id("skkind", i), PortText("Your seat"),
            PortText("Play it yourself, or make it a computer and watch: every seat a computer, the whole map shown, with "
                     "the replay\xe2\x80\x99s controls and speeds up to 16 times Fastest."));
    else if (RoomMode() != 0)
      TipOn(Id("skkind", i), PortText("The seat"),
            PortText("Open: the next person in the room to join takes it. A person seated here goes back to the room "
                     "when the seat is made a computer, closed or open. The host may give up their own seat to run "
                     "the room, and take any seat that is not a person's again."));
    if (nKind >= 0 && values[nKind] != s.nKind) {
      s.nKind = values[nKind];
      s.name.clear();
      s.chip.clear();
      s.bReady = false;
      if (s.nKind == KIND_COMPUTER && i != 0)
        s.nColour = FreeColour(static_cast<size_t>(i));
      NoteSlots("a slot");
    }
  }
  if (!bSeated) {
    Decl empty;
    empty.d.layout.layoutDirection = CLAY_TOP_TO_BOTTOM;
    empty.d.layout.sizing.width = Grow();
    empty.d.layout.childAlignment.x = CLAY_ALIGN_X_CENTER;
    Open(empty);
    Text(s.nKind == KIND_OPEN ? PortText("OPEN") : PortText("CLOSED"), UI_FACE_WIDE, Fs(16), 0.06f, kLbInkEmpty);
    if (s.nKind == KIND_OPEN && !bCompact)
      Text(PortText("for the next person to join"), UI_FACE_TEXT, Fs(13), 0.0f, kLbInkDesc);
    Close();
  }
  Close();

  if (bSeated) {
    Decl tiles;
    tiles.d.layout.childGap = Gap(Fs(5));
    tiles.d.layout.childAlignment.y = CLAY_ALIGN_Y_CENTER;
    Open(tiles);
    if (g_nColourBy == 1 && HasTeams()) {
      // colours by team: the row's shade of its team's colour, set by the team
      SlotShade("skshade", i, RowRgb(s), Keep(g_teamNames[s.nTeam]), kSwatchW);
      TipOn(Id("skshade", i), PortText("Colours by team"),
            PortText("Each team plays in shades of its colour. Choose Colours: By player to pick each player\xe2\x80\x99s."));
    } else if (s.nColour >= 0 && ColoursChosen()) {
      Swatch(i);
    } else {
      Decl by;
      by.d.layout.sizing.width = Fixed(Sc(kSwatchW));
      by.d.layout.sizing.height = Fixed(Sc(36));
      by.d.layout.padding = Pad(Fs(10), Fs(10), 0, 0);
      by.d.layout.childAlignment.y = CLAY_ALIGN_Y_CENTER;
      by.d.border = Border(kScWellRim, 1, 1, 1, 1);
      Radius(by, 5);
      Open(by);
      Text(UseMapSettings() ? PortText("The map\xe2\x80\x99s") : PortText("By slot"), UI_FACE_TEXT, Fs(14), 0.0f,
           kScInkMenu);
      Close();
    }
    std::vector<int> values;
    int nComputer = 0;  // the slot's place among the computers
    for (int k = 1; k < i; ++k)
      nComputer += g_slots[k].nKind == KIND_COMPUTER;
    const std::vector<std::string> names = RaceNames(bMine, nComputer < 7, &values);
    int nCur = static_cast<int>(names.size()) - 1;
    for (size_t k = 0; k < values.size(); ++k)
      if (values[k] == s.nRace)
        nCur = static_cast<int>(k);
    // a room's people choose their own races; a guest sees the host's computers'
    const bool bRaceFree = bMine || (s.nKind == KIND_COMPUTER && !UseMapSettings() && !Guest());
    const int nRace = Pulldown("skrace", i, 130, names, nCur, bRaceFree ? PD_ON : PD_OFF);
    if (nRace >= 0) {
      s.nRace = values[nRace];
      if (bMine)
        g_qwRaceAt = SDL_GetTicks();
      NoteSlots("a race");
    }
    if (!bRaceFree)
      TipOn(Id("skrace", i),
            s.nKind == KIND_PERSON ? PortText("Their race")
            : UseMapSettings()     ? PortText("Use Map Settings")
                                   : PortText("The host\xe2\x80\x99s"),
            s.nKind == KIND_PERSON ? PortText("Each person in the room chooses their own race.")
            : UseMapSettings()     ? PortText("The map gives the computers their races.")
                                   : PortText("The host chooses the computers\xe2\x80\x99 races."));
    // the handicap (port_handicap.h): each person's their own, the computers' the host's, as in
    // StarCraft II's lobby
    const bool bHandicapFree = bMine || (s.nKind == KIND_COMPUTER && !Guest());
    std::vector<std::string> handicaps;
    int nHandicapAt = 0;
    for (int k = 0; k < HandicapChoices(); ++k) {
      handicaps.push_back(std::to_string(HandicapChoice(k)) + "%");
      if (HandicapChoice(k) == s.nHandicap)
        nHandicapAt = k;
    }
    const int nHandicap = Pulldown("skhcap", i, 92, handicaps, nHandicapAt, bHandicapFree ? PD_ON : PD_OFF);
    if (nHandicap >= 0 && HandicapChoice(nHandicap) != s.nHandicap) {
      s.nHandicap = HandicapChoice(nHandicap);
      if (bMine)
        g_qwHandicapAt = SDL_GetTicks();
      NoteSlots("a handicap");
    }
    TipOn(Id("skhcap", i),
          bHandicapFree            ? PortText("Handicap")
          : s.nKind == KIND_PERSON ? PortText("Their handicap")
                                   : PortText("The host\xe2\x80\x99s"),
          bHandicapFree            ? PortText(kHandicapTip)
          : s.nKind == KIND_PERSON ? PortText("Each person in the room chooses their own handicap.")
                                   : PortText("The host chooses the computers\xe2\x80\x99 handicaps."));
    if (s.nKind == KIND_PERSON && !bMine) {
      // a room's person: the seat chip (how they play, on what, how far from the host) and
      // Ready, in the level's and personality's place (the design's seat chip)
      Decl chip = Box("skchip", i);
      chip.d.layout.sizing.width = Fixed(Sc(200 + 150 + 5));
      chip.d.layout.sizing.height = Fixed(Sc(36));
      chip.d.layout.padding = Pad(Fs(10), Fs(10), 0, 0);
      chip.d.layout.childGap = Gap(Fs(8));
      chip.d.layout.childAlignment.y = CLAY_ALIGN_Y_CENTER;
      chip.d.border = Border(kScWellRim, 1, 1, 1, 1);
      Radius(chip, 5);
      Open(chip);
      Decl what;
      what.d.layout.sizing.width = Grow();
      Open(what);
      Text(Keep(s.chip), UI_FACE_TEXT, Fs(14), 0.0f, kScInkMenu);
      Close();
      Text(s.bReady ? PortText("READY") : PortText("NOT READY"), UI_FACE_WIDE, Fs(12), 0.04f,
           s.bReady ? kScInkAccept : kLbInkDesc);
      Close();
    } else if (s.nKind == KIND_COMPUTER) {
      const bool bComputer = !UseMapSettings() && RoomMode() == 0;
      const int nLevel = Pulldown("sklevel", i, 200, LevelNames(), bComputer ? s.nLevel : AI_LEVEL_CLASSIC,
                                  bComputer ? PD_ON : PD_OFF, NULL, NULL, 200);
      if (nLevel >= 0) {
        s.nLevel = nLevel;
        NoteSlots("a computer level");
      }
      TipOn(Id("sklevel", i), PortText("Computer level"),
            UseMapSettings()  ? PortText("Use Map Settings: the map runs its computers.")
            : RoomMode() != 0 ? PortText("A room\xe2\x80\x99s computers are the original\xe2\x80\x99s own, Classic AI. Other "
                                         "levels in a room are not available yet.")
                              : PortText(kLevelTip));
      PulldownLater("skpers", i, 150, PortText("Its own script"), PortText("Personality"),
                    PortText("How the computer plays: turtle, rusher, expander and the like. Not available yet."));
    } else {
      Decl gap;
      gap.d.layout.sizing.width = Fixed(Sc(200 + 150 + 5));
      Open(gap);
      Close();
    }
    if (HasTeams()) {
      // the row's team (lobby.js slotRow's t-team): one of the teams, or a new one
      std::vector<std::string> teams;
      std::vector<unsigned> chips;
      TeamChoices(g_teamNames, &teams, &chips);
      const int nTeam = Pulldown("skteam", i, 140, teams, s.nTeam, PD_ON, &chips[0]);
      if (nTeam >= 0 && nTeam != s.nTeam) {
        JoinTeam(static_cast<size_t>(i), nTeam);
        NoteSlots("a team");
      }
    }
    Close();
  }
  Close();
}

// Distinct Colours, a button of the bar over the slots
void DistinctButton(void)
{
  Decl dc = Box("skdistinct");
  dc.d.layout.sizing.height = Fixed(Sc(36));
  const int nClash = Clashes();
  if (ScMini(dc,
             nClash ? Keep(PortTextF("Distinct Colours ({0})", {std::to_string(nClash)}))
                    : PortText("Distinct Colours"),
             'T', CanDistinctColours()))
    DistinctColours();
  TipOn(Id("skdistinct"), PortText("Distinct colours"),
        nClash ? PortText("Some players\xe2\x80\x99 colours are close to another\xe2\x80\x99s. Gives each player the colour "
                          "farthest from those already taken; you keep yours. Hotkey: T")
               : PortText("No two colours are close. Gives each player the colour farthest from those already taken; you "
                          "keep yours. Hotkey: T"));
}

// The bar over the slots (lobby.js tools): the counts, Fill Empty, everyone's settings at
// once, Distinct Colours, and how the list is drawn. fWidth is the bar's, in pixels: where the
// first row cannot hold Distinct Colours on one line, it goes to the second, by the list's
// layout (the user, 2026-10-26: its label wrapped below the button).
void SlotTools(float fWidth)
{
  int nOn = 0, nComputers = 0, nEmpty = 0, nPeople = 0, nReady = 0, nOpen = 0;
  for (const Slot &s : g_slots) {
    nOn += Seated(s);
    nComputers += s.nKind == KIND_COMPUTER;
    nEmpty += s.nKind == KIND_CLOSED;
    nOpen += s.nKind == KIND_OPEN;
    nPeople += s.nKind == KIND_PERSON || s.nKind == KIND_YOU;
    nReady += s.nKind == KIND_PERSON && s.bReady;
  }
  if (RoomMode() != 0) {
    // a room's lobby: its people and who is ready, and the host's tools after
    Decl bar;
    bar.d.layout.sizing.width = Grow();
    bar.d.layout.padding = Pad(Fs(16), Fs(16), Fs(10), Fs(10));
    bar.d.layout.childGap = Gap(Fs(16));
    bar.d.layout.childAlignment.y = CLAY_ALIGN_Y_CENTER;
    bar.d.border = Border(kLbDivider, 0, 0, 0, Fs(2));
    Open(bar);
    Decl cnt;
    cnt.d.layout.layoutDirection = CLAY_TOP_TO_BOTTOM;
    Open(cnt);
    Text(Keep(PortTextF("{0} of {1}", {std::to_string(nOn), std::to_string(g_slots.size())})), UI_FACE_BOLD, Fs(19),
         0.0f, kLbInkInfo);
    int nOthers = 0;
    for (const Slot &s : g_slots)
      nOthers += s.nKind == KIND_PERSON && !s.bYou;
    std::string facts = PortTextNF("{0} person", "{0} people", nPeople, {std::to_string(nPeople)}) + kDot +
                        PortTextNF("{0} computer", "{0} computers", nComputers, {std::to_string(nComputers)});
    if (nOpen)
      facts += kDot + PortTextF("{0} open", {std::to_string(nOpen)});
    if (nOthers)
      facts += kDot + PortTextF("{0} of {1} ready", {std::to_string(nReady), std::to_string(nOthers)});
    Text(Keep(facts), UI_FACE_TEXT, Fs(14), 0.0f, kLbInkDesc);
    Close();
    Spacer();
    if (!Guest()) {
      Decl fill = Box("skfill");
      fill.d.layout.sizing.height = Fixed(Sc(36));
      if (ScMini(fill, PortText("Fill Empty"), 'F', CanFillEmpty()))
        FillEmpty();
      TipOn(Id("skfill"), PortText("Fill empty"),
            PortText("A computer of a random race in each closed seat; open seats wait for people. Hotkey: F"));
    }
    Close();
    return;
  }
  Decl bar;
  bar.d.layout.layoutDirection = CLAY_TOP_TO_BOTTOM;
  bar.d.layout.sizing.width = Grow();
  bar.d.layout.padding = Pad(Fs(16), Fs(16), Fs(10), Fs(10));
  bar.d.layout.childGap = Gap(Fs(8));
  bar.d.border = Border(kLbDivider, 0, 0, 0, Fs(2));
  Open(bar);
  Decl one;
  one.d.layout.sizing.width = Grow();
  one.d.layout.childGap = Gap(Fs(8));
  one.d.layout.childAlignment.y = CLAY_ALIGN_Y_CENTER;
  Open(one);
  Decl cnt;
  cnt.d.layout.layoutDirection = CLAY_TOP_TO_BOTTOM;
  cnt.d.layout.sizing.width = Fixed(Sc(150));
  Open(cnt);
  Text(Keep(PortTextF("{0} of {1}", {std::to_string(nOn), std::to_string(g_slots.size())})), UI_FACE_BOLD, Fs(19), 0.0f,
       kLbInkInfo);
  std::string counts = PortTextNF("{0} computer", "{0} computers", nComputers, {std::to_string(nComputers)});
  if (nEmpty)
    counts += kDot + PortTextF("{0} empty", {std::to_string(nEmpty)});
  Text(Keep(counts), UI_FACE_TEXT, Fs(14), 0.0f, kLbInkDesc);
  Close();
  Decl fill = Box("skfill");
  fill.d.layout.sizing.height = Fixed(Sc(36));
  if (ScMini(fill, PortText("Fill Empty"), 'F', CanFillEmpty()))
    FillEmpty();
  Decl lbl;
  lbl.d.layout.padding = Pad(Fs(6), 0, 0, 0);
  Open(lbl);
  Label(PortText("All computers:"), 15, kLbInkDesc);
  Close();
  // the computers' race, at once
  std::vector<int> values;
  const std::vector<std::string> names = RaceNames(false, true, &values);
  int nSame = -2;
  for (size_t i = 0; i < g_slots.size(); ++i)
    if (g_slots[i].nKind == KIND_COMPUTER)
      nSame = nSame == -2 ? g_slots[i].nRace : nSame == g_slots[i].nRace ? nSame : -1;
  int nCur = -1;
  for (size_t k = 0; k < values.size(); ++k)
    if (values[k] == nSame)
      nCur = static_cast<int>(k);
  const int nAll = Pulldown("skallrace", 0, 130, names, nCur, nComputers && !UseMapSettings() ? PD_ON : PD_OFF, NULL,
                            nCur < 0 ? PortText("Race") : NULL);
  if (nAll >= 0) {
    for (size_t i = 0; i < g_slots.size(); ++i)
      if (g_slots[i].nKind == KIND_COMPUTER)
        g_slots[i].nRace = values[nAll];
    NoteSlots("every computer's race");
  }
  // the computers' level, at once
  int nSameLevel = -2;
  for (size_t i = 0; i < g_slots.size(); ++i)
    if (g_slots[i].nKind == KIND_COMPUTER)
      nSameLevel = nSameLevel == -2 ? g_slots[i].nLevel : nSameLevel == g_slots[i].nLevel ? nSameLevel : -1;
  const int nAllLevel = Pulldown("skalllevel", 0, 200, LevelNames(), nSameLevel < 0 ? -1 : nSameLevel,
                                 nComputers && !UseMapSettings() ? PD_ON : PD_OFF, NULL,
                                 nSameLevel < 0 ? PortText("Level") : NULL, 190);
  if (nAllLevel >= 0) {
    for (size_t i = 0; i < g_slots.size(); ++i)
      if (g_slots[i].nKind == KIND_COMPUTER)
        g_slots[i].nLevel = nAllLevel;
    NoteSlots("every computer's level");
  }
  TipOn(Id("skalllevel", 0), PortText("Every computer\xe2\x80\x99s level"), PortText(kLevelTip));
  PulldownLater("skallpers", 0, 150, PortText("Its own script"), PortText("Every computer\xe2\x80\x99s personality"),
                PortText("How the computers play: turtle, rusher, expander and the like. Not available yet."));
  // the first row's width with the button, every piece at its own width and the label on one line
  const float fRow = Sc(16) * 2 + Sc(150) + Sc(8) * 6 + Sc(44) +
                     UiTextWidth(UI_FACE_TEXT, Sc(16), 0.0f, PortText("Fill Empty")) + Sc(6) +
                     UiTextWidth(UI_FACE_TEXT, Sc(15), 0.0f, PortText("All computers:")) + Sc(130 + 200 + 150) +
                     Sc(44) + UiTextWidth(UI_FACE_TEXT, Sc(16), 0.0f,
                                          PortTextF("Distinct Colours ({0})", {"00"}).c_str());
  const bool bDistinctBelow = fRow > fWidth;
  if (!bDistinctBelow)
    DistinctButton();
  Close();
  Decl two;
  two.d.layout.sizing.width = Grow();
  two.d.layout.childAlignment.y = CLAY_ALIGN_Y_CENTER;
  Open(two);
  if (TeamsOffered()) {
    Decl nt = Box("sknewteam");
    nt.d.layout.sizing.width = Fixed(Sc(150));
    nt.d.layout.sizing.height = Fixed(Sc(36));
    if (ScMini(nt, PortText("New Team"), 'N', g_teamNames.size() < g_slots.size())) {
      TeamsAdd(&g_teamNames);
      g_nLayout = -1;
      TeamsChanged();
      NoteSlots("New Team");
    }
    TipOn(Id("sknewteam"), PortText("New team"),
          PortText("Adds a team; choose it in a player\xe2\x80\x99s Team list to move them to it. Hotkey: N"));
    // Quick layout (teams.js presets): sets the teams from where the map's starts are, and
    // each player's start in their team's part; a player's Team list changes it after
    const std::vector<TeamLayout> &ls = Layouts();
    std::vector<std::string> items;
    std::vector<int> which;
    QuickLayoutChoices(ls, &items, &which);
    const int nQuick =
        Pulldown("skquick", 0, 190, items, -1, which.empty() ? PD_OFF : PD_ON, NULL, PortText("Quick layout"), 380);
    if (nQuick >= 0 && nQuick < static_cast<int>(which.size())) {
      ApplyLayout(which[nQuick]);
      NoteSlots("a quick layout");
    }
    TipOn(Id("skquick"), PortText("Quick layout"),
          which.empty()
              ? PortText("This map\xe2\x80\x99s starts could not be read, so no layout can be worked out from them. "
                         "New Team and each player\xe2\x80\x99s Team list make the teams.")
              : PortText("Sets the teams from where the map\xe2\x80\x99s starts are: halves of the map, or groups of "
                         "nearby starts. Each player starts in their team\xe2\x80\x99s part (Start positions: By "
                         "slot). Move anyone with their Team list after."));
    Decl lbl2;
    lbl2.d.layout.padding = Pad(Fs(6), 0, 0, 0);
    Open(lbl2);
    Label(g_nLayout >= 0 && g_nLayout < static_cast<int>(ls.size())
              ? Keep(ls[g_nLayout].name + " \xc2\xb7 " + ls[g_nLayout].vs)
              : PortText("Teams by hand"),
          15, kLbInkDesc);
    Close();
  }
  Spacer();
  if (bDistinctBelow) {
    DistinctButton();
    Decl gap;
    gap.d.layout.sizing.width = Fixed(Sc(8));
    Open(gap);
    Close();
  }
  static const char *const kDensity[] = {PT_("Auto"), PT_("Rows"), PT_("Compact"), PT_("Columns")};
  const char *const apDensity[] = {PortText(kDensity[0]), PortText(kDensity[1]), PortText(kDensity[2]),
                                   PortText(kDensity[3])};
  const int nD = Segments("skdensity", apDensity, 4, g_nDensity, 1u << 3,
                          PortText("Each team a column of chips, for games past 24 players. Not available yet."));
  if (nD >= 0)
    g_nDensity = nD;
  Close();
  Close();
}

// The slots box: the bar, the players' frame, the observers' (lobby.js teamFrame,
// observers). Single player has no teams to frame, so the players are one frame.
void SlotsBox(float x, float y, float w, float h)
{
  Decl box = At("skslots", x, y, w, h);
  box.d.layout.layoutDirection = CLAY_TOP_TO_BOTTOM;
  InnerBox(box);
  Open(box);
  SlotTools(Sc(w));

  Decl scroll = Box("skscroll");
  scroll.d.layout.layoutDirection = CLAY_TOP_TO_BOTTOM;
  scroll.d.layout.sizing.width = Grow();
  scroll.d.layout.sizing.height = Grow();
  scroll.d.layout.padding = Pad(Fs(16), Fs(10), Fs(12), Fs(14));
  scroll.d.layout.childGap = Gap(Fs(10));
  scroll.d.clip.vertical = true;
  Open(scroll);

  const bool bCompact = Compact();
  const int nSlots = static_cast<int>(g_slots.size());
  const int nMaxFirst = nSlots > g_nSlotShown ? nSlots - g_nSlotShown : 0;
  g_nSlotFirst += WheelRows(Over(Id("skscroll")), 1);  // its rows are tall
  g_nSlotFirst = g_nSlotFirst < 0 ? 0 : g_nSlotFirst > nMaxFirst ? nMaxFirst : g_nSlotFirst;
  const int nSlotEnd = nSlots < g_nSlotFirst + g_nSlotShown ? nSlots : g_nSlotFirst + g_nSlotShown;
  int nComputers = 0;
  for (const Slot &s : g_slots)
    nComputers += s.nKind == KIND_COMPUTER;

  // the rows in the frames' order: with teams, each team's rows together (lobby.js groups),
  // the list scrolling through them; without, one frame of every row
  const bool bTeams = HasTeams();
  const int nFrames = bTeams ? static_cast<int>(g_teamNames.size()) : 1;
  std::vector<int> order;
  for (int q = 0; q < nFrames; ++q)
    for (int i = 0; i < nSlots; ++i)
      if (!bTeams || g_slots[i].nTeam == q)
        order.push_back(i);
  int nDrawn = 0;
  for (int q = 0; q < nFrames; ++q) {
    std::vector<int> rows;
    int nMembers = 0;
    for (int k = 0; k < nSlots; ++k) {
      if (bTeams && g_slots[order[k]].nTeam != q)
        continue;
      ++nMembers;
      if (k >= g_nSlotFirst && k < nSlotEnd)
        rows.push_back(order[k]);
    }
    // a team none of whose rows is in view is left out, an empty one shown to be joined
    if (rows.empty() && (nMembers > 0 || g_bFolded))
      continue;
    const bool bFirst = nDrawn++ == 0;
    Decl team = Box("skteamframe", q);
    team.d.layout.layoutDirection = CLAY_TOP_TO_BOTTOM;
    team.d.layout.sizing.width = Grow();
    team.d.layout.padding = Pad(Fs(4), Fs(4), 0, Fs(10));
    InnerBox(team, 1);
    Open(team);
    Decl head;
    head.d.layout.sizing.width = Grow();
    head.d.layout.sizing.height = Fixed(Sc(bCompact ? 40 : 51));
    head.d.layout.padding = Pad(Fs(4), Fs(8), 0, 0);
    head.d.layout.childGap = Gap(Fs(10));
    head.d.layout.childAlignment.y = CLAY_ALIGN_Y_CENTER;
    Open(head);
    if (bFirst) {
      Decl fold = Box("skfold");
      const bool bFoldHot = Over(fold.d.id);
      if (Clicked(fold.d.id))
        g_bFolded = !g_bFolded;
      fold.d.layout.sizing.width = Fixed(Sc(28));
      fold.d.layout.sizing.height = Fixed(Sc(28));
      fold.d.custom.customData = Custom(DrawFold, kScButton, 0.0f, (g_bFolded ? 0 : 1) | (bFoldHot ? 2 : 0));
      Open(fold);
      Close();
    }
    if (bTeams) {
      Chip(TeamBase(q), 16);
      std::string name = g_teamNames[q];
      for (char &c : name)
        c = static_cast<char>(toupper(static_cast<unsigned char>(c)));
      Text(Keep(name), UI_FACE_WIDE, Fs(16), 0.04f, kScInkHead);
      Text(Keep(nMembers == 0 ? std::string(PortText("empty: choose it in a player\xe2\x80\x99s Team list"))
                              : PortTextNF("{0} player", "{0} players", nMembers, {std::to_string(nMembers)})),
           UI_FACE_TEXT, Fs(15), 0.0f, kLbInkDesc);
    } else {
      Text(PortText("PLAYERS"), UI_FACE_WIDE, Fs(16), 0.04f, kScInkHead);
      Text(Keep(PortTextF("{0} slots", {std::to_string(nSlots)}) + kDot +
                PortTextNF("{0} computer", "{0} computers", nComputers, {std::to_string(nComputers)})),
           UI_FACE_TEXT, Fs(15), 0.0f, kLbInkDesc);
    }
    if (g_bFolded && bFirst) {
      Decl strip;
      strip.d.layout.childGap = Gap(Fs(3));
      Open(strip);
      for (const Slot &s : g_slots) {
        Decl sw;
        sw.d.layout.sizing.width = Fixed(Sc(12));
        sw.d.layout.sizing.height = Fixed(Sc(12));
        if (s.nKind != KIND_CLOSED && (s.nColour >= 0 || (g_nColourBy == 1 && bTeams)))
          sw.d.backgroundColor = Col(UiRgb(RowRgb(s)));
        sw.d.border = Border(s.nKind == KIND_CLOSED ? kScWellRim : kScBlack, 1, 1, 1, 1);
        Open(sw);
        Close();
      }
      Close();
    }
    Spacer();
    if (!g_bFolded && nMaxFirst > 0 && bFirst)
      Text(Keep(PortTextF("{0} to {1} shown, the wheel scrolls",
                          {std::to_string(g_nSlotFirst + 1), std::to_string(nSlotEnd)})),
           UI_FACE_TEXT, Fs(14), 0.0f, kScInkLabel);
    Close();
    if (UseMapSettings() && !g_bFolded) {
      Decl ums;
      ums.d.layout.padding = Pad(Fs(12), Fs(12), 0, Fs(6));
      Open(ums);
      Text(PortText("Use Map Settings: the map seats the players and runs the game."), UI_FACE_TEXT, Fs(15), 0.0f,
           kScInkMsg);
      Close();
    }
    if (!g_bFolded)
      for (int i : rows)
        SlotRow(i);
    Close();
  }
  g_nFrames = nDrawn > 0 ? nDrawn : 1;

  // the observers: they watch a multiplayer game, and in skirmish the player when their seat
  // is a computer's
  Decl obs = Box("skobsframe", 0);
  obs.d.layout.layoutDirection = CLAY_TOP_TO_BOTTOM;
  obs.d.layout.sizing.width = Grow();
  obs.d.layout.padding = Pad(Fs(12), Fs(12), 0, Fs(10));
  obs.d.border = Border(kScWellRim, 1, 1, 1, 1);
  Radius(obs, 4);
  Open(obs);
  Decl oh;
  oh.d.layout.sizing.height = Fixed(Sc(46));
  oh.d.layout.childGap = Gap(Fs(10));
  oh.d.layout.childAlignment.y = CLAY_ALIGN_Y_CENTER;
  Open(oh);
  Text(PortText("OBSERVERS"), UI_FACE_WIDE, Fs(16), 0.04f, kScInkMenu);
  // a room's: who may watch, and how far behind the players (the console's Settings)
  const PortJson &room = LanClientRoom();
  const bool bWatching = room["watching"].kind != PortJson::BOOL || room["watching"].b;
  const bool bWatch = Watching();
  const bool bSlot = RoomMode() == 0 && WatchOffered() && !g_slots.empty();
  Text(RoomMode() == 0 ? (bWatch ? PortText("You") : bSlot ? PortText("Nobody") : PortText("Multiplayer only"))
       : bWatching     ? PortText("Anyone in the room")
                       : PortText("Nobody"),
       UI_FACE_TEXT, Fs(15), 0.0f, bWatch ? kScInkMenu : kScInkOff);
  Close();
  if (bSlot)
    ObserverRow(bWatch);  // the skirmish's own: the player watches, every seat a computer
  else
    Text(RoomMode() == 0
             ? PortText("Observers watch a multiplayer game without playing. They come with multiplayer, which is "
                        "not available yet.")
         : bWatching
             ? Keep(PortTextF("The room\xe2\x80\x99s people who are not seated can watch the match once it starts, "
                              "{0} s behind the players.",
                              {std::to_string(static_cast<int>(room["delay"].Num(10)))}))
             : PortText("The host has turned watching off."),
         UI_FACE_TEXT, Fs(14), 0.0f, kScInkOff, -1, 1.35f);
  Close();
  Close();  // the scroll
  Close();  // the box

  // how many rows fit, for the next frame: the scroll's height less its padding, the frame's
  // head and foot, and the observers' frame as it was last drawn
  const Clay_ElementData sc = Clay_GetElementData(Id("skscroll"));
  const Clay_ElementData of = Clay_GetElementData(Id("skobsframe", 0));
  if (sc.found) {
    const float fRoom = sc.boundingBox.height - Sc(12 + 14) - g_nFrames * (Sc(bCompact ? 40 : 51) + Sc(10 + 10)) -
                        Sc(10) - (of.found ? of.boundingBox.height : Sc(46 + 40));
    const int n = static_cast<int>(fRoom / Sc(bCompact ? 40 : 69));
    g_nSlotShown = n > 1 ? n : 1;
  }
}

// The game options' rows (LobbyMatchOptionsFrame), in SC2's units.
const float kOptionHeight = 54;

void Option(const char *pszName, const char *pszId, const std::vector<std::string> &items, int *pnCur,
            const char *pszNote = NULL)
{
  Open(OptionRow(kOptionHeight));
  OptionName(pszName, pszNote);
  // a guest sees the host's choice
  const int n = Pulldown(pszId, 0, 188, items, *pnCur, Guest() ? PD_LOCKED : PD_ON);
  if (Guest())
    TipOn(Id(pszId), pszName, PortText("The host chooses it."));
  if (n >= 0)
    *pnCur = n;
  Close();
}

// What the Mutators tab and the options' word on it read from the set-up.
MutatorsSetUp MutatorsOfSetUp(void)
{
  MutatorsSetUp s;
  s.pSet = &g_mut;
  s.bMine = !Guest();
  s.pMap = g_pic.of == g_map.name ? &g_pic : NULL;
  s.pszMapTitle = g_map.title.c_str();
  // the speed's English name: the Mutators tab reads the frame's length from it, and translates
  // it where it shows it
  s.pszSpeed = kSpeeds[g_nSpeed < 0 ? 0 : g_nSpeed > 6 ? 6 : g_nSpeed];
  s.bMapUnits = UseMapSettings();
  return s;
}

// The options, h high; with bSummary, the mutators in a word at their foot (a set-up with no box
// of the host's under them).
void NoteMutators(void);

void OptionsBox(float x, float y, float h, bool bSummary)
{
  Decl box = At("skopts", x, y, 438, h);
  box.d.layout.layoutDirection = CLAY_TOP_TO_BOTTOM;
  box.d.layout.padding = Pad(Fs(20), Fs(15), Fs(15), Fs(15));
  box.d.layout.childGap = Gap(Fs(4));
  InnerBox(box);
  Open(box);
  Decl t;
  t.d.layout.sizing.height = Fixed(Sc(36));
  Open(t);
  Text(PortText("GAME OPTIONS"), UI_FACE_WIDE, Fs(16), 0.04f, kScInkHead);
  Close();
  std::vector<std::string> types;
  for (const GameType &gt : g_types)
    types.push_back(gt.name);
  const int nTypeWas = g_nType;
  Option(PortText("Game type"), "sktype", types, &g_nType,
         Watching() && g_types[g_nType].params.bCategory == 2 && FreeForAll() >= 0
             ? PortText("Free For All while you watch")
             : NULL);
  if (g_nType != nTypeWas)
    ListSubs();
  if (TeamsOffered())
    TipOn(Id("sktype"), PortText("Team Match"),
          PortText("Players on teams: teammates are allied, share vision and win together, and every other team is an enemy. "
                   "Quick layout sets the teams from the map\xe2\x80\x99s starts; New Team and each player\xe2\x80\x99s Team "
                   "list change them. Plays by Hyperion\xe2\x80\x99s rules."));
  if (!g_subs.empty()) {
    std::vector<std::string> subs;
    for (const GameType &gt : g_subs)
      subs.push_back(gt.name);
    Option(g_types[g_nType].params.bCategory == 0x0F ? PortText("Top vs Bottom") : PortText("Teams"), "sksub", subs,
           &g_nSub);
  }
  std::vector<std::string> speeds;
  for (int k = 0; k < 7; ++k)
    speeds.push_back(PortText(kSpeeds[k]));
  if (Watching()) {
    // a watched game plays as a replay, which goes past Fastest
    for (int m = 2; m <= 16; m *= 2)
      speeds.push_back(PortTextF("Fastest \xc3\x97{0}", {std::to_string(m)}));
    int nCur = g_nSpeed;
    for (int m = g_nWatchMul; g_nSpeed == 6 && m > 1; m /= 2)
      ++nCur;
    Option(PortText("Game speed"), "skspeed", speeds, &nCur);
    g_nSpeed = nCur < 7 ? nCur : 6;
    g_nWatchMul = nCur < 7 ? 1 : 1 << (nCur - 6);
  } else {
    Option(PortText("Game speed"), "skspeed", speeds, &g_nSpeed);
  }

  // the rules: either, unless the game is past 1.17's limits or a mod brings its own data
  const std::string forced = ForcedWhy();
  Open(OptionRow(kOptionHeight));
  OptionName(PortText("Rules"), NULL);
  const std::vector<std::string> rules = {PortText("Classic"), "Hyperion"};
  // a guest's shows the host's choice
  const int nRule = Pulldown("skrules", 0, 188, rules, forced.empty() ? (g_nRules == 1 ? 1 : 0) : 1,
                             forced.empty() && !Guest() ? PD_ON : PD_LOCKED);
  if (nRule >= 0)
    g_nRules = nRule;
  if (Guest())
    TipOn(Id("skrules"), PortText("Rule set"),
          PortText("The host chooses the rules the room\xe2\x80\x99s match plays by."));
  else if (forced.empty())
    TipOn(Id("skrules"), PortText("Rule set"),
          PortText("Classic plays as the original does, to the frame. Hyperion has the improvements, starting with pathing. The "
                   "replay keeps the choice."));
  else
    // {0} why the game must play by Hyperion's rules
    TipOn(Id("skrules"), PortText("Hyperion rules, required"),
          Keep(PortTextF("Because of {0}. The original never had such games, so there is nothing to match.", {forced})));
  Close();

  // the unit limit, held with the mutators (port_mutators.h): a guest's shows the host's
  Open(OptionRow(kOptionHeight));
  OptionName(PortText("Unit limit"), NULL);
  std::vector<std::string> limits;
  for (int i = 0; i < MutatorUnitLimits(); ++i)
    limits.push_back(MutatorUnitLimitName(i));
  const int nLimit = Pulldown("skunits", 0, 188, limits, MutatorUnitLimitOf(g_mut.nUnits), Guest() ? PD_LOCKED : PD_ON);
  if (nLimit >= 0 && MutatorUnitLimit(nLimit) != g_mut.nUnits) {
    g_mut.nUnits = MutatorUnitLimit(nLimit);
    NoteMutators();
  }
  if (Guest())
    TipOn(Id("skunits"), PortText("Unit limit"), PortText("The host chooses the most units the game holds."));
  else
    TipOn(Id("skunits"), PortText("Unit limit"),
          PortText("The most units the game holds at once, every player\xe2\x80\x99s and the map\xe2\x80\x99s minerals and "
                   "geysers together. The original holds 1,700. Automatic: the original\xe2\x80\x99s in a game it could "
                   "play, and in a bigger game or with a supply cap past 200, room for the map\xe2\x80\x99s own and for each "
                   "player\xe2\x80\x99s whole supply cap in units. A number plays by Hyperion\xe2\x80\x99s rules."));
  Close();

  Open(OptionRow(kOptionHeight));
  OptionName(PortText("Start positions"), UseMapSettings() ? PortText("The map sets them") : NULL);
  if (StartsOffered() && StartsKnown()) {
    const std::vector<std::string> starts = {PortText("Random"), PortText("By slot")};
    const int nStarts = Pulldown("skstarts", 0, 188, starts, g_nStartsBy);
    if (nStarts >= 0)
      g_nStartsBy = nStarts;
    TipOn(Id("skstarts"), PortText("Start positions"),
          PortText("Random: the original draws each player a start. By slot: each player starts where the map\xe2\x80\x99s "
                   "picture shows their number. Either plays by the original\xe2\x80\x99s rules."));
  } else {
    PulldownLater("skstarts", 0, 188, UseMapSettings() ? PortText("The map\xe2\x80\x99s") : PortText("Random"),
                  PortText("Start positions"),
                  UseMapSettings()
                      ? PortText("The map places the players.")
                      : PortText("This game type draws the starts at random. Choosing them is not available for it."));
  }
  Close();

  if (TeamsOffered()) {
    Open(OptionRow(kOptionHeight));
    OptionName(PortText("Colours"), NULL);
    const std::vector<std::string> by = {PortText("By player"), PortText("By team")};
    const int nBy = Pulldown("skcolourby", 0, 188, by, g_nColourBy);
    if (nBy >= 0) {
      g_nColourBy = nBy;
      TeamsChanged();
    }
    TipOn(Id("skcolourby"), PortText("Colours"),
          PortText("By player: each player\xe2\x80\x99s own colour. By team: each team in shades of one colour, so a team "
                   "reads at a glance."));
    Close();
  }

  Decl lock = Box("sklock");
  lock.d.layout.sizing.height = Fixed(Sc(54));
  lock.d.layout.padding = Pad(Fs(10), 0, 0, 0);
  lock.d.layout.childGap = Gap(Fs(12));
  lock.d.layout.childAlignment.y = CLAY_ALIGN_Y_CENTER;
  Open(lock);
  Decl check;
  check.d.layout.sizing.width = Fixed(Sc(26));
  check.d.layout.sizing.height = Fixed(Sc(26));
  check.d.custom.customData = Custom(DrawScCheck, kScButton, 0.0f, 1 | 8);
  Open(check);
  Close();
  Text(PortText("Lock teams"), UI_FACE_BOLD, Fs(17), 0.0f, kScInkOff);
  Close();
  TipOn(Id("sklock"), PortText("Lock teams"),
        PortText("Keeps players from changing team in a multiplayer lobby. Not available yet."));
  if (bSummary) {
    Decl room;
    room.d.layout.sizing.height = Grow();
    Open(room);
    Close();
    if (MutatorsSummary(MutatorsOfSetUp()))
      g_nView = 1;
  }
  Close();
}

// The map's picture, its name and its size, w wide and h high; the picture grows with the box, to
// square.
void InfoBox(float x, float y, float w, float h)
{
  Decl box = At("skinfo", x, y, w, h);
  box.d.layout.layoutDirection = CLAY_TOP_TO_BOTTOM;
  box.d.layout.padding = Pad(Fs(11), Fs(11), Fs(10), Fs(10));
  box.d.layout.childGap = Gap(Fs(3));
  InnerBox(box);
  Open(box);
  const float fPic = w - 26;
  Preview("skpreview", &g_map, fPic, fminf(fPic, fmaxf(260, h - 170)), 30);
  Decl gap;
  gap.d.layout.sizing.height = Fixed(Sc(7));
  Open(gap);
  Close();
  Text(Keep(g_map.title), UI_FACE_BOLD, Fs(24), 0.0f, kLbInkInfo);
  // {3} the map's tileset
  Text(Keep(PortTextF("{0} starts \xc2\xb7 {1} \xc3\x97 {2} tiles \xc2\xb7 {3}",
                      {std::to_string(g_map.players), std::to_string(g_map.w), std::to_string(g_map.h),
                       PortText(kTilesets[g_map.tileset & 7])})),
       UI_FACE_TEXT, Fs(16), 0.0f, kLbInkDesc);
  Text(UseMapSettings() ? PortText("The map places the players.")
       : g_nStartsBy == 1 && StartsOffered() && StartsKnown()
           ? PortText("Starts by slot: each number is that player\xe2\x80\x99s start.")
           : PortText("Random starts: the numbers are the map\xe2\x80\x99s, not the players\xe2\x80\x99."),
       UI_FACE_TEXT, Fs(14), 0.0f, kScInkOff);
  Decl big = Floating("skenlarge", 0, -Sc(11), -Sc(10), 22, CLAY_ATTACH_POINT_RIGHT_BOTTOM, CLAY_ATTACH_POINT_RIGHT_BOTTOM);
  big.d.layout.sizing.height = Fixed(Sc(32));
  if (ScMini(big, PortText("Enlarge"), 0))
    g_bEnlarge = true;
  Close();
}

// The mutators (port_mutators.h) have the right column's second tab
// (port_front_mutators.h). Any one other than 1.17's plays by Hyperion's rules (ForcedWhy). In a
// room the host chooses them and a guest sees the host's.
void NoteMutators(void)
{
  FrontLog("skirmish: mutators: %s\n", MutatorsStandard(g_mut) ? "classic" : MutatorsText(g_mut).c_str());
}

void SetupStep(void)
{
  if (g_map.name.empty()) {
    g_nStep = STEP_MAPS;
    return;
  }
  // the map's starts and minimap, unless the browser over it is showing another
  if (!g_bBrowse)
    ReadStarts(g_map.name.c_str());
  // Team Match with no teams yet (it was just chosen, or the map changed): its first teams
  if (TeamsOffered() && g_teamNames.size() <= 1 && !g_bBrowse) {
    FirstTeams();
    NoteSlots("Team Match");
  }
  const float fW = (UiWidth() - 2 * Sc(58)) / Sc(1), fH = (UiHeight() - Sc(132) - Sc(78)) / Sc(1);
  // the map's column narrows, to 300 at the least, where the slots would be narrower than a
  // computer's row in a team game, 1,210 with the colour kSwatchW wide (the user, 2026-10-26: at
  // 16:9 the rows ran under the options; the map can be enlarged)
  const float fInfoW = fmaxf(300, fminf(425, fW - 34 - 10 - 438 - 10 - 13 - 1210));
  const float fInfoX = fW - 34 - fInfoW, fOptsX = fInfoX - 10 - 438;
  const float fFootY = fH - 33 - 52;

  // the header: the map, the line under it, a note, and the map's buttons
  Decl head = At("skhead", 40, 22, fW - 40 - (g_pHost != NULL ? 1040 : 500), 0);
  head.d.layout.layoutDirection = CLAY_TOP_TO_BOTTOM;
  head.d.layout.childGap = Gap(Fs(2));
  Open(head);
  Text(Keep(g_map.title), UI_FACE_BOLD, Fs(27), 0.0f, kScInkHi);
  // the game, and the mod whose data this run loaded on it: both shown, so a player sees what
  // the game plays with (the user, 2026-10-27)
  const char *pszBase = gbIsExpansion ? "Brood War" : "StarCraft";
  const bool bModData = ModDataId()[0] != '\0';
  // {0} the mod's name, {1} the game it is built on
  const std::string gameName = bModData ? PortTextF("{0} on {1}", {ModDataName(), pszBase}) : std::string(pszBase);
  const std::string line = std::string(g_pHost != NULL ? PortText(g_pHost->pszLine) : PortText("Skirmish")) + kDot +
                           gameName + kDot + PortTextF("{0} starts", {std::to_string(g_slots.size())}) + kDot +
                           std::to_string(g_map.w) + " \xc3\x97 " + std::to_string(g_map.h) + kDot +
                           PortText(kTilesets[g_map.tileset & 7]) + kDot + MapKind(g_map.file);
  Text(Keep(line), UI_FACE_TEXT, Fs(17), 0.0f, kLbInkInfo);
  if (!g_note.empty())
    Text(Keep(g_note), UI_FACE_TEXT, Fs(15), 0.0f, kScInkAccept);
  Close();
  Decl hb = Floating("skheadbtns", 0, -Sc(34), Sc(32), 21, CLAY_ATTACH_POINT_RIGHT_TOP, CLAY_ATTACH_POINT_RIGHT_TOP);
  hb.d.layout.childGap = Gap(Fs(8));
  Open(hb);
  if (g_pHost != NULL && g_pHost->pfnHead != NULL)
    g_pHost->pfnHead();
  Decl game = Box("skgame");
  game.d.layout.sizing.width = Fixed(Sc(240));
  game.d.layout.sizing.height = Fixed(Sc(40));
  const std::string button = bModData ? PortTextF("Mod: {0}", {ModDataName()}) : PortTextF("Game: {0}", {pszBase});
  ScMini(game, Keep(button), 'E', false);
  if (bModData) {
    TipOn(Id("skgame"), PortText("The game"),
          Keep(PortTextF("{0}, a mod on {1}: its units, weapons and rules are in every game started here. The Mods "
                         "screen chooses another mod, or the original game.",
                         {ModDataName(), pszBase})));
  } else {
    TipOn(Id("skgame"), PortText("The game"),
          PortText("StarCraft, Brood War or a mod built on one, chosen for each game: it sets the races, the maps and the "
                   "rules. For now it is the one Hyperion started in; choosing it here is not available yet."));
  }
  Decl change = Box("skchange");
  change.d.layout.sizing.width = Fixed(Sc(200));
  change.d.layout.sizing.height = Fixed(Sc(40));
  const bool bChange = ScMini(change, PortText("Change Map"), 'A', !Guest());
  if (Guest())
    TipOn(Id("skchange"), PortText("The map"), PortText("The host chooses the map."));
  Close();

  // The right column is two tabs (the design's take A2, 2026-10-26): Game Options with the map,
  // and Mutators, which takes the column. A host's own box (a room's, the trainer's) stays under
  // the options and the map; with none, the options say at their foot which mutators are on.
  SlotsBox(13, 93, fOptsX - 10 - 13, fFootY - 10 - 93);
  const float fColTop = 93 + 46, fBottom = fFootY - 10, fColW = 438 + 10 + fInfoW;
  const bool bShared = g_pHost != NULL && g_pHost->pfnBox != NULL;
  const float fBoxH = bShared ? 462 : fBottom - fColTop;
  const MutatorsSetUp setUp = MutatorsOfSetUp();
  if (g_nView == 1) {
    if (MutatorsTab(fOptsX, fColTop, fColW, fBottom - fColTop, setUp))
      NoteMutators();
  } else {
    OptionsBox(fOptsX, fColTop, fBoxH, !bShared);
    InfoBox(fInfoX, fColTop, fInfoW, fBoxH);
    if (bShared) {
      const float fBoxY = fColTop + fBoxH + 6;
      g_pHost->pfnBox(fOptsX, fBoxY, fColW, fBottom - fBoxY);
    }
  }
  // the tabs after the boxes, so they draw over the boxes' rims
  const int nView = MutatorsViewTabs(fOptsX, 93, fColW, g_nView, g_mut);
  if (nView >= 0)
    g_nView = nView;

  // the foot: Start Game, Back, and the rules the game plays by
  const float fStartW = g_pHost != NULL ? 320 : 250;  // the host's label may be longer
  Decl start = At("skstart", 40, fFootY, fStartW, 52);
  const char *pszStart = g_pHost == NULL             ? PT_("Start Game")
                         : g_pHost->pfnStartLabel != NULL ? g_pHost->pfnStartLabel()
                                                          : g_pHost->pszStart;
  const bool bStart = ScButton(start, PortText(pszStart), 'G', true);
  Decl back = At("skback2", 40 + fStartW + 6, fFootY, 250, 52);
  const bool bBack = ScButton(back, Guest() ? PortText("Leave") : PortText("Back"), 'B', false);
  // the rules' mark, only where a choice requires Hyperion's (a mod, a team game, a computer
  // level, mutators): its name alone, the reason in its tip. The rules chosen in Game Options,
  // Classic's or the default Hyperion's, leave the place empty (the user, 2026-10-26)
  const float fRulesX = 40 + fStartW + 250 + 6 + 24;
  const std::string forced = ForcedWhy();
  if (!forced.empty()) {
    const bool bMod = ModDataId()[0] != '\0';
    Decl rules = At("skrulestrip", fRulesX, fFootY, 0, 52);
    rules.d.layout.sizing.width = Fit();
    rules.d.layout.padding = Pad(Fs(22), Fs(22), 0, 0);
    rules.d.layout.childAlignment.y = CLAY_ALIGN_Y_CENTER;
    rules.d.backgroundColor = Col(kLbInner);
    rules.d.border = Border(kLbHyperionRim, Fs(2), Fs(2), Fs(2), Fs(2));
    Radius(rules, 4);
    Open(rules);
    Text(bMod ? PortText("MOD RULES") : PortText("HYPERION RULES"), UI_FACE_WIDE, Fs(15), 0.04f, kScInkAccept);
    Close();
    // {0} why the game must play by those rules
    TipOn(Id("skrulestrip"), bMod ? PortText("Mod rules") : PortText("Hyperion rules"),
          Keep(PortTextF("Required by {0}. Its replay and save are Hyperion\xe2\x80\x99s and say so.", {forced})));
  }

  if (bStart)
    Start();
  else if (bBack && Guest()) {
    g_lbOpen.clear();
    g_pHost->pfnBack();
  } else if (bBack) {
    g_nStep = STEP_MAPS;
    g_lbOpen.clear();
    g_note.clear();
    ShowSetUpMap();
  } else if (bChange) {
    g_bBrowse = true;
    g_lbOpen.clear();
    g_note.clear();
    ShowSetUpMap();
  }
}

// The map's picture, enlarged, with who plays (lobby.js enlarge).
void EnlargeDialog(void)
{
  const float fW = 1100, fH = fminf(1040.0f, UiHeight() / Sc(1) - 160);
  const bool bOut = DialogOpen("skbig", fW, fH, 80);
  Text(Keep(g_map.title + kDot + PortTextF("{0} starts", {std::to_string(g_map.players)})), UI_FACE_WIDE, Fs(15),
       0.03f, kScInkTipHead);
  Decl pic;
  pic.d.layout.sizing.width = Grow();
  pic.d.layout.childAlignment.x = CLAY_ALIGN_X_CENTER;
  Open(pic);
  Preview("skbigpic", &g_map, fW - 68, fH - 54 - 40 - 50 - 64 - 36, 75, true);
  Close();
  Decl legend;
  legend.d.layout.childGap = Gap(Fs(14));
  legend.d.layout.childAlignment.y = CLAY_ALIGN_Y_CENTER;
  Open(legend);
  for (size_t i = 0; i < g_slots.size(); ++i) {
    const Slot &s = g_slots[i];
    if (s.nKind == KIND_CLOSED)
      continue;
    Decl it;
    it.d.layout.childGap = Gap(Fs(5));
    it.d.layout.childAlignment.y = CLAY_ALIGN_Y_CENTER;
    Open(it);
    Text(Keep(std::to_string(i + 1)), UI_FACE_BOLD, Fs(14), 0.0f, kLbInkInfo);
    if (s.nColour >= 0)
      Chip(Chosen(s), 12);
    Text(s.nKind == KIND_YOU      ? Keep(std::string(gLocalPlayerName))
         : s.nKind == KIND_PERSON ? Keep(s.name)
         : s.nKind == KIND_OPEN   ? PortText("Open")
                                  : PortText("Computer"),
         UI_FACE_TEXT, Fs(14), 0.0f, kLbInkDesc);
    Close();
  }
  Close();
  Decl row;
  row.d.layout.sizing.width = Grow();
  row.d.layout.childAlignment.x = CLAY_ALIGN_X_CENTER;
  Open(row);
  Decl close = Box("skbigclose");
  close.d.layout.sizing.width = Fixed(Sc(250));
  close.d.layout.sizing.height = Fixed(Sc(52));
  const bool bClose = ScButton(close, PortText("Close"), 'C', false);
  Close();
  Close();  // the dialog
  if (bClose || bOut)
    g_bEnlarge = false;
}

}  // namespace

int PortFrontSkirmishSpeed(void)
{
  return g_nSpeed >= 0 && g_nSpeed <= 6 ? g_nSpeed : 6;
}

namespace FrontKit {

void SkirmishOpen(int nSpeed, const SkirmishHost *pHost)
{
  // the host's own set-up in place of the one shown
  const char *pszWas = g_pHost != NULL ? g_pHost->pszId : "";
  const char *pszNow = pHost != NULL ? pHost->pszId : "";
  std::string typeHeld;
  int nSubHeld = -1;
  if (strcmp(pszWas, pszNow) != 0) {
    g_held[pszWas] = HoldSetUp();
    const auto it = g_held.find(pszNow);
    const Held h = it != g_held.end() ? it->second : Held();
    TakeSetUp(h);
    typeHeld = h.type;
    nSubHeld = h.nSub;
  }
  g_pHost = pHost;
  g_nResult = 0;
  g_note.clear();
  g_lbOpen.clear();
  g_bBrowse = false;
  g_bEnlarge = false;
  // 1.17's speed for a first set-up; one that waits keeps its own
  if (g_map.name.empty())
    g_nSpeed = nSpeed >= 0 && nSpeed <= 6 ? nSpeed : 6;
  // a set-up made for another profile starts this one on its own race
  if (!g_slots.empty() && g_slotsFor != ProfileCurrent()) {
    g_slots[0].nRace = LastRace();
    g_slotsFor = ProfileCurrent();
  }
  if (g_nRules < 0)
    g_nRules = RulesChosenClassic() ? 0 : 1;  // Hyperion's unless classic is chosen (hxrules.h)
  // the game types, keeping the one chosen before
  const std::string typeWas = nSubHeld >= 0 ? typeHeld
                              : g_nType < static_cast<int>(g_types.size()) ? g_types[g_nType].name
                                                                          : std::string();
  const int nSubWas = nSubHeld >= 0 ? nSubHeld : g_nSub;
  g_types.clear();
  IterateMainTemplates(AddType);
  // Team Match after Free For All, on its template: every player for themselves until the
  // teams ally them (port_teams.h)
  for (size_t i = 0; i < g_types.size(); ++i)
    if (g_types[i].params.bCategory == 3) {
      GameType t = g_types[i];
      t.name = "Team Match";
      t.bTeamMatch = true;
      g_types.insert(g_types.begin() + static_cast<std::ptrdiff_t>(i) + 1, t);
      break;
    }
  // a room's game is made in 1.17's own lobby, which seats Melee and Free For All as it is
  // told to (port_lanlobby.cpp); the others wait for their own seating
  if (pHost != NULL && pHost->nRoom != 0)
    for (size_t i = g_types.size(); i-- > 0;)
      if (g_types[i].bTeamMatch || (g_types[i].params.bCategory != 2 && g_types[i].params.bCategory != 3))
        g_types.erase(g_types.begin() + static_cast<std::ptrdiff_t>(i));
  g_nType = 0;
  for (size_t i = 0; i < g_types.size(); ++i)
    if (typeWas.empty() ? g_types[i].params.bCategory == 2 : g_types[i].name == typeWas)
      g_nType = static_cast<int>(i);
  ListSubs();
  if (nSubWas < static_cast<int>(g_subs.size()))
    g_nSub = nSubWas;
  // 1.17's list is made again (a game empties it). The screen comes back on the set-up after a
  // game it made, a host's after any: the trainer's after a session from a file or from here
  g_pic.of.clear();
  ListFolder();
  g_nStep = STEP_MAPS;
  if ((g_bPlayed || g_pHost != NULL) && !g_map.name.empty() && !g_slots.empty())
    g_nStep = STEP_SETUP;
  g_bPlayed = false;
  ShowSetUpMap();
  // HYPERION_FRONT_MAP=<folder>\<file>, under maps: the set-up of that map, once, for a check
  // without a person
  static bool s_bMapOpened;
  if (const char *pszMap = SDL_getenv("HYPERION_FRONT_MAP"))
    if (!s_bMapOpened) {
      s_bMapOpened = true;
      std::string dir = "maps";
      const char *pszSlash = strrchr(pszMap, '\\');
      if (pszSlash != NULL)
        dir += "\\" + std::string(pszMap, pszSlash);
      OpenFolder(dir.c_str());
      const char *pszFile = pszSlash != NULL ? pszSlash + 1 : pszMap;
      for (size_t i = 0; i < g_rows.size(); ++i) {
        const char *pszName = g_rows[i].p->szName;
        const size_t nName = strlen(pszName), nFile = strlen(pszFile);
        if (IsMap(g_rows[i]) && nName >= nFile && lstrcmpiA(pszName + nName - nFile, pszFile) == 0) {
          SetUp(static_cast<int>(i));
          break;
        }
      }
      // HYPERION_FRONT_WATCH=<speed>[:<level>,...] with it: the player's seat a computer's, the
      // speed 0 to 10 (7 to 10 Fastest x2 to x16), the seats' levels in order (port_ai.h), and
      // Start Game, to watch the game (port_watchgame.h)
      if (const char *pszWatch = SDL_getenv("HYPERION_FRONT_WATCH"))
        if (g_nStep == STEP_SETUP && WatchOffered() && !g_slots.empty()) {
          g_slots[0].nKind = KIND_COMPUTER;
          const int nStep = SDL_atoi(pszWatch);
          g_nSpeed = nStep >= 0 && nStep <= 6 ? nStep : 6;
          g_nWatchMul = nStep > 6 && nStep <= 10 ? 1 << (nStep - 6) : 1;
          const char *p = strchr(pszWatch, ':');
          for (size_t k = 0; p != NULL && k < g_slots.size(); ++k) {
            const int nLevel = SDL_atoi(p + 1);
            if (g_slots[k].nKind == KIND_COMPUTER && nLevel >= 0 && nLevel < AI_LEVELS)
              g_slots[k].nLevel = nLevel;
            p = strchr(p + 1, ',');
          }
          NoteSlots("HYPERION_FRONT_WATCH");
          StartGame();
        }
      // HYPERION_FRONT_MUTATORS=<n>[:<list>] with it: the Mutators tab, the nth in its list briefed
      // (Up and Down's order), or Game Options' for -, with --mutators' list as the set
      if (const char *pszMuts = SDL_getenv("HYPERION_FRONT_MUTATORS"))
        if (g_nStep == STEP_SETUP) {
          g_nView = pszMuts[0] == '-' ? 0 : 1;
          const char *pszList = strchr(pszMuts, ':');
          if (pszList != NULL && MutatorsChooseList(pszList + 1))
            g_mut = Session().mut;
          MutatorsTabBrief(SDL_atoi(pszMuts));
          NoteMutators();
        }
    }
  FrontLog("skirmish: %u maps and folders in %s, %u game types%s\n", static_cast<unsigned>(g_rows.size()), g_szDir,
           static_cast<unsigned>(g_types.size()), g_nStep == STEP_SETUP ? ", back on the set-up" : "");
}

int SkirmishResult(void)
{
  return g_nResult;
}

void SkirmishPlayed(std::string *pTitle, std::string *pType, std::vector<std::string> *pTeams)
{
  *pTitle = g_map.title;
  *pType = g_nType < static_cast<int>(g_types.size()) ? g_types[g_nType].name : std::string();
  pTeams->clear();
  if (HasTeams())
    *pTeams = g_teamNames;
}

// As StartGame checks the set-up and seats it, but kept as a record (port_frontscreens.h);
// PortLaunchSkirmishGame seats it the same way.
bool SkirmishGameNow(SkirmishGame *pOut)
{
  if (g_map.name.empty() || g_nType >= static_cast<int>(g_types.size())) {
    g_note = PortText("Choose a map first.");
    return false;
  }
  int nComputers = 0;
  for (size_t i = 1; i < g_slots.size(); ++i)
    nComputers += g_slots[i].nKind == KIND_COMPUTER;
  if (!UseMapSettings() && nComputers == 0) {
    g_note = PortText("A session needs at least one computer.");
    return false;
  }
  char szPath[0x104];
  if (MapEntry() == NULL || !FindMapFile(g_map.name.c_str(), szPath, sizeof szPath)) {
    g_note = PortText("That map is no longer there.");
    return false;
  }
  SkirmishGame g = SkirmishRecord(nComputers);
  g.map = szPath;
  ProfileSetLastRace(RaceKeep(g_slots[0].nRace));
  g_note.clear();
  g_bPlayed = true;
  *pOut = g;
  return true;
}

bool SkirmishRoomNow(SkirmishRoomSetUp *pOut)
{
  if (RoomMode() == 0 || g_map.name.empty() || g_nType >= static_cast<int>(g_types.size()))
    return false;
  char szPath[0x104];
  if (!FindMapFile(g_map.name.c_str(), szPath, sizeof szPath))
    return false;
  const TGAMEPARAMS &t = g_subs.empty() ? g_types[g_nType].params : g_subs[g_nSub].params;
  SkirmishRoomSetUp r;
  r.map = szPath;
  r.title = g_map.title;
  r.type = g_types[g_nType].name;
  r.nKey = t.bCategory | t.bSub << 8 | t.wType << 16;
  r.nSpeed = g_nSpeed;
  r.nPlayers = static_cast<int>(g_slots.size());
  for (const Slot &s : g_slots) {
    SkirmishRoomSeat seat;
    seat.owner = s.nKind == KIND_COMPUTER ? "computer" : s.nKind == KIND_CLOSED ? "closed" : s.nKind == KIND_OPEN ? "open"
                                                                                                      : "player";
    seat.nRace = s.nRace;
    seat.nHandicap = s.nHandicap;
    seat.name = s.name;
    seat.bReady = s.bReady;
    seat.bYou = Mine(s);
    r.seats.push_back(seat);
  }
  r.bHyperion = HyperionRules();
  r.mut = g_mut;
  *pOut = r;
  return true;
}

void SkirmishRoomSeats(const std::vector<SkirmishRoomSeat> &seats)
{
  if (RoomMode() == 0)
    return;
  const bool bRaceHeld = SDL_GetTicks() - g_qwRaceAt < 1500;
  const bool bHandicapHeld = SDL_GetTicks() - g_qwHandicapAt < 1500;
  for (size_t i = 0; i < g_slots.size(); ++i) {
    Slot &s = g_slots[i];
    const SkirmishRoomSeat *p = i < seats.size() ? &seats[i] : NULL;
    const bool bPerson = p != NULL && p->owner == "player";
    if (Guest()) {
      // a guest's set-up is the room's
      s.nKind = p == NULL ? KIND_CLOSED : bPerson ? KIND_PERSON : p->owner == "computer" ? KIND_COMPUTER
                : p->owner == "open" ? KIND_OPEN : KIND_CLOSED;
    } else if (s.nKind == KIND_YOU) {
      // the host's own seat is its screen's; the name is the one the room knows it by
      if (p != NULL && p->bYou)
        s.name = p->name;
      continue;
    } else if (bPerson && !p->bYou && (s.nKind == KIND_OPEN || s.nKind == KIND_PERSON)) {
      // a person sits only in an open seat; one the host has just made a computer or closed
      // keeps the host's choice until the room has heard it
      s.nKind = KIND_PERSON;
    } else if (!bPerson && s.nKind == KIND_PERSON) {
      s.nKind = KIND_OPEN;
    }
    if (s.nKind == KIND_PERSON && p != NULL) {
      s.name = p->name;
      s.chip = p->chip;
      s.bReady = p->bReady;
      s.bYou = p->bYou;
      if (!(s.bYou && bRaceHeld))
        s.nRace = p->nRace;
      if (!(s.bYou && bHandicapHeld))
        s.nHandicap = p->nHandicap;
    } else {
      s.name.clear();
      s.chip.clear();
      s.bReady = s.bYou = false;
      if (Guest() && p != NULL) {
        s.nRace = p->nRace;
        s.nHandicap = p->nHandicap;
      }
    }
  }
}

void SkirmishGuestSetUp(const SkirmishRoomSetUp &r)
{
  if (!Guest())
    return;
  if (g_map.name != r.map || g_slots.size() != r.seats.size()) {
    g_map = MapInfo();
    g_map.name = r.map;
    const size_t nSlash = r.map.find_last_of("\\/");
    g_map.file = nSlash == std::string::npos ? r.map : r.map.substr(nSlash + 1);
    g_map.title = r.title;
    g_map.players = r.nPlayers;
    g_slots.assign(r.seats.size() > 1 ? r.seats.size() : 2, Slot{KIND_OPEN, RACE_RANDOM, -1});
    g_pic = MapPicture();
    g_nSlotFirst = 0;
    NoTeams();
    g_nStep = STEP_SETUP;
  }
  // the map's size and tileset, once its file has come and been read: tried again each second
  // until then
  static Uint64 s_qwTried;
  if ((g_pic.of != g_map.name || g_pic.w == 0) && SDL_GetTicks() - s_qwTried > 1000) {
    s_qwTried = SDL_GetTicks();
    g_pic = MapPicture();
    ReadStarts(g_map.name.c_str());
  }
  if (g_pic.of == g_map.name && g_pic.w > 0) {
    g_map.w = g_pic.w;
    g_map.h = g_pic.h;
    g_map.tileset = g_pic.tileset;
  }
  for (size_t i = 0; i < g_types.size(); ++i)
    if (g_types[i].name == r.type && g_nType != static_cast<int>(i)) {
      g_nType = static_cast<int>(i);
      ListSubs();
    }
  g_nSpeed = r.nSpeed >= 0 && r.nSpeed <= 6 ? r.nSpeed : 6;
  g_nRules = r.bHyperion ? 1 : 0;
  g_mut = r.mut;
  SkirmishRoomSeats(r.seats);
}

void SkirmishScreen(void)
{
  MapPreviewFrame();
  Decl panel = Box("skpanel");
  panel.d.layout.layoutDirection = CLAY_TOP_TO_BOTTOM;
  // the design's frame (lobby.css .lb-frame): 58 units in, 132 down, ending 78 above the
  // foot so the taskbar stays clear
  panel.d.layout.sizing.width = Fixed(UiWidth() - 2 * Sc(58));
  panel.d.layout.sizing.height = Fixed(UiHeight() - Sc(132) - Sc(78));
  if (g_nStep == STEP_MAPS) {
    panel.d.layout.padding = Pad(Fs(40), Fs(34), Fs(26), Fs(33));
    panel.d.layout.childGap = Gap(Fs(12));
  }
  panel.d.floating.attachTo = CLAY_ATTACH_TO_ROOT;
  panel.d.floating.offset.x = Sc(58);
  panel.d.floating.offset.y = Sc(132);
  panel.d.floating.zIndex = 20;
  static UiCustom frame = {0.0f, DrawScFrame, NULL};
  panel.d.custom.customData = &frame;
  Open(panel);
  if (g_nStep == STEP_MAPS)
    MapStep();
  else
    SetupStep();
  Close();
  if (g_nStep == STEP_SETUP && g_bBrowse)
    ChangeMapDialog();
  else if (g_nStep == STEP_SETUP && g_bEnlarge)
    EnlargeDialog();
  DrawTip();
  PulldownCloseOutside();
}

bool SkirmishKeys(void)
{
  if (ColourAnyTyping())
    return true;  // the colour picker's entry has the keys
  if (PulldownKeys())
    return true;
  if (g_bBrowse)
    return BrowserKeys(true);
  if (g_bEnlarge) {
    if (Key(VK_ESCAPE) || Key('C') || Key(VK_RETURN))
      g_bEnlarge = false;
    return true;
  }
  if (g_pHost != NULL && g_pHost->pfnKeys != NULL && g_pHost->pfnKeys())
    return true;
  if (g_nStep == STEP_MAPS)
    return BrowserKeys(false);
  // the right column's tabs, and the Mutators tab's arrows; Esc there goes back to the options
  if (Key('O')) {
    g_nView = 0;
    return true;
  }
  if (Key('U')) {
    g_nView = 1;
    return true;
  }
  if (g_nView == 1) {
    bool bChanged = false;
    if (MutatorsTabKeys(MutatorsOfSetUp(), &bChanged)) {
      if (bChanged)
        NoteMutators();
      return true;
    }
    if (Key(VK_ESCAPE)) {
      g_nView = 0;
      return true;
    }
  }
  if (Guest()) {
    // a guest's set-up: Ready, and Leave
    if (Key('G') || Key(VK_RETURN))
      Start();
    else if (Key(VK_ESCAPE) || Key('B'))
      g_pHost->pfnBack();
    return true;
  }
  if (Key('G') || Key(VK_RETURN))
    Start();
  else if (Key('F') && CanFillEmpty())
    FillEmpty();
  else if (Key('T') && CanDistinctColours())
    DistinctColours();
  else if (Key('A')) {
    g_bBrowse = true;
    g_note.clear();
    ShowSetUpMap();
  } else if (Key(VK_ESCAPE) || Key('B')) {
    g_nStep = STEP_MAPS;
    g_note.clear();
    ShowSetUpMap();
  }
  return true;
}

}  // namespace FrontKit
