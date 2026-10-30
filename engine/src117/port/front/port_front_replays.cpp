// port_front_replays.cpp -- Hyperion's replays screen (docs/modules/front-end.md).
// Presentation only: a replay plays as 1.17's own screen
// starts it (port_launch.cpp).
//
// My Replays lists the replays folder (maps\replays in the data folder and the folders in it,
// AutoSave's copies with them), LastReplay first and then the newest, each read for the list
// and not played (ReplayHxPeek): its format, map, players and length, and whether this build
// plays it, with the reason when it does not. The side shows the one picked: its map and
// starts, from the map the replay carries, the verdict, the players, what the file says about
// itself, and Watch, Rename, Show in Folder and Delete. Online, Leaderboards and Replay TV
// wait for Hyperion Online, and are greyed with a tip that says so.
#include <windows.h>
#include <shellapi.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <algorithm>
#include <string>
#include <vector>
#include "../gameplay/port_launch.h"  // PortLaunchReplayFile
#include "../platform/port_paths.h"   // PortExplorer
#include "oc_data.h"           // OC_VA, where 1.17's data is
#include "Storm.h"             // SStrCopy
#include "grid.h"              // IsBroodwar
#include "../../h/hxsim.h"        // HX_SIM_REVISION
#include "../../h/hxrules.h"      // HX_RULE_HYPERION
#include "../../h/port_hxreplay.h"  // ReplayHxPeek
#include "port_frontkit.h"
#include "port_frontlobby.h"
#include "port_frontscreens.h"
#include "port_ui.h"
#include "../data/port_stormapi.h"  // SFileGetUserDataPath
#include "../platform/port_util.h"  // PortSpeedFrameMs
#include "../render/port_maptext.h"  // MapTextUtf8
#include "../../gen/globals117.h"

using namespace FrontKit;

namespace {

// What a replay's verdict and format say, in colours each theme leaves as they are.
const UiColor kOkInk = KitRgb(0x74e3a4);
const UiColor kOkFill = KitRgb(0x0a5032, 0.3f);
const UiColor kNoInk = KitRgb(0xff7a6a);
const UiColor kNoFill = KitRgb(0x78120a, 0.32f);
const UiColor kHxInk = KitRgb(0xffe490);
const UiColor k117Ink = KitRgb(0x9eccf3);
// the races' inks (replays.css .race)
const UiColor kRaceInk[] = {KitRgb(0xd79bff), KitRgb(0x8fc8ff), KitRgb(0xffd86e)};
const char *const kRaceNames[] = {PT_("Zerg"), PT_("Terran"), PT_("Protoss")};
const char kRaceLetters[] = {'Z', 'T', 'P'};

const char *const kTabs[] = {PT_("My Replays"), PT_("Online"), PT_("Leaderboards"), PT_("Replay TV")};
const char *const kFormats[] = {PT_("Any format"), PT_("Classic"), PT_("Hyperion")};

// The panel (replays.css .rp): under the tabs, as wide as the lobby's, the list on the left
// and the side, 520 units, on the right; and the list's columns.
const float kTop = 176, kSide = 520;
const float kCols[] = {200, 190, 80, 120, 40};  // map, players, length, played, state

enum { VERDICT_OK, VERDICT_NO, VERDICT_BAD };

struct Verdict {
  int nKind;
  std::string head, line, needs;
};

struct Player {
  std::string name;
  int nRace;    // 0 Zerg, 1 Terran, 2 Protoss, else none
  int nType;    // 1 a computer, 2 a person
  int nTeam;
  int nColour;  // kSlotColours, or -1
};

struct Replay {
  std::string path;  // the file
  std::string rel;   // under maps\replays
  std::string name;  // its name, without .rep
  FILETIME when;
  bool bOk = false;  // read
  ReplayHxInfo info;
  std::vector<Player> players;
  std::string map, type, matchup;
  bool bExpansion = false;
  unsigned dwFrames = 0;
  int nSpeed = 6;  // the speed it was recorded at, Slowest (0) to Fastest (6)
  int nWidth = 0, nHeight = 0, nTileset = 0, nGameType = 0;
  unsigned nMission = 0;
};

std::string g_root;                 // maps\replays in the data folder, with its backslash
std::string g_keep;                 // the replay picked when the screen closed
std::vector<Replay> g_reps;
std::vector<int> g_shown;           // the replays the filters pass, in order
int g_nPick = -1;                   // the replay picked, in g_reps
int g_nFirst;                       // the first row shown
int g_nShownRows = 12;              // how many rows the list showed last frame
bool g_bReveal;                     // bring the pick into view
int g_nFormat;
int g_nGame;
int g_nWho;                         // 0 every profile, else a name in g_who
std::vector<std::string> g_who;
std::string g_search;
bool g_bSearch;                     // the search box has the keyboard
std::string g_note;
int g_nResult;
MapPicture g_pic;                   // the picked replay's map
bool g_bRename;
std::string g_newName;
bool g_bDelete;
float g_fSideScroll;                // the side, scrolled down this far
float g_fSideMax;                   // as far as it scrolls, from the last frame

// ---------------------------------------------------------------------------
// Reading the folder.

ULONGLONG Ticks(const FILETIME &ft)
{
  return static_cast<ULONGLONG>(ft.dwHighDateTime) << 32 | ft.dwLowDateTime;
}

const char *const kSpeedNames[kPortSpeeds] = {PT_("Slowest"), PT_("Slower"), PT_("Slow"),  PT_("Normal"),
                                              PT_("Fast"),    PT_("Faster"), PT_("Fastest")};

// A replay's length at the speed it was recorded at, as 1.17's replay panel counts it.
std::string Clock(unsigned dwFrames, int nSpeed)
{
  const unsigned s = static_cast<unsigned>(static_cast<unsigned long long>(dwFrames) * PortSpeedFrameMs(nSpeed) / 1000);
  char sz[32];
  if (s >= 3600)
    snprintf(sz, sizeof sz, "%u:%02u:%02u", s / 3600, s / 60 % 60, s % 60);
  else
    snprintf(sz, sizeof sz, "%u:%02u", s / 60, s % 60);
  return sz;
}

std::string Age(const FILETIME &ft)
{
  FILETIME now;
  GetSystemTimeAsFileTime(&now);
  const double h = Ticks(now) > Ticks(ft) ? (Ticks(now) - Ticks(ft)) / 36e9 : 0.0;
  if (h < 1.0 / 60)
    return PortText("just now");
  if (h < 1) {
    const int m = static_cast<int>(h * 60);
    return PortTextNF("a minute ago", "{0} minutes ago", m, {std::to_string(m)});
  }
  if (h < 24) {
    const int n = static_cast<int>(h);
    return PortTextNF("an hour ago", "{0} hours ago", n, {std::to_string(n)});
  }
  if (h < 48)
    return PortText("yesterday");
  if (h < 24 * 14) {
    const int n = static_cast<int>(h / 24);
    return PortTextNF("{0} day ago", "{0} days ago", n, {std::to_string(n)});
  }
  FILETIME local;
  SYSTEMTIME st;
  FileTimeToLocalFileTime(&ft, &local);
  FileTimeToSystemTime(&local, &st);
  // day and month, in the language's order
  return PortTextF("{0} {1}", {std::to_string(st.wDay), PortTextC("month", kMonths[(st.wMonth + 11) % 12])});
}

// A string from the file, n bytes at most, in UTF-8: StarCraft's colour codes (bytes under 32)
// left out, UTF-8 kept, and the rest Windows-1252 as 1.17 writes it (MapTextUtf8).
std::string Field(const unsigned char *p, size_t n)
{
  const char *psz = reinterpret_cast<const char *>(p);
  return MapTextUtf8(std::string(psz, SDL_strnlen(psz, n)).c_str());
}

// Who played and against whom: two players by their races (TvZ), teams by their sizes (2v2),
// and more players with no teams as a free for all. Teams count in the team types and Use Map
// Settings' forces.
std::string Matchup(const Replay &r)
{
  const bool bTeams = r.nGameType == 0x0A || (r.nGameType >= 0x0B && r.nGameType <= 0x0F);
  std::vector<int> teams, sizes;
  for (const Player &p : r.players) {
    const int t = bTeams ? p.nTeam : static_cast<int>(teams.size()) + 1000;
    const auto it = std::find(teams.begin(), teams.end(), t);
    if (it == teams.end()) {
      teams.push_back(t);
      sizes.push_back(1);
    } else {
      ++sizes[it - teams.begin()];
    }
  }
  if (r.players.empty())
    return "";
  if (r.players.size() == 2 && teams.size() == 2) {
    std::string s;
    for (const Player &p : r.players) {
      if (!s.empty())
        s += "v";
      s += p.nRace >= 0 && p.nRace < 3 ? kRaceLetters[p.nRace] : 'R';
    }
    return s;
  }
  if (teams.size() == r.players.size())
    return PortTextF("{0} players, free for all", {std::to_string(r.players.size())});
  std::string s;
  for (int n : sizes)
    s += (s.empty() ? "" : "v") + std::to_string(n);
  return s;
}

// The header's player records, and a Hyperion replay's past 1.17's 12: a person or a computer
// in each, with the colour of the slot it played from.
void ReadPlayers(Replay &r)
{
  r.players.clear();
  const unsigned char *pSlots = r.info.abHead + 0xA1;
  const size_t nPast = r.info.past.size() / 36;
  for (size_t i = 0; i < 12 + nPast; ++i) {
    const unsigned char *s = i < 12 ? pSlots + i * 36 : r.info.past.data() + (i - 12) * 36;
    if (s[8] != 1 && s[8] != 2)
      continue;
    Player p;
    p.nType = s[8];
    p.nRace = s[9] < 3 ? s[9] : -1;
    p.nTeam = s[10];
    p.name = Field(s + 11, 25);
    const DWORD dwId = *reinterpret_cast<const DWORD *>(s);
    p.nColour = dwId < 8 ? static_cast<int>(dwId) : -1;
    r.players.push_back(p);
  }
}

void ReadReplay(Replay &r)
{
  r.bOk = ReplayHxPeek(r.path.c_str(), &r.info, NULL);
  if (!r.bOk)
    return;
  const unsigned char *h = r.info.abHead;
  r.bExpansion = h[0] != 0;
  r.dwFrames = *reinterpret_cast<const DWORD *>(h + 0x01);
  r.nSpeed = h[0x3A];  // the game's speed, which 1.17's replay panel counts the time by
  r.nMission = *reinterpret_cast<const WORD *>(h + 0x05);
  // the game header at 0x14, its description at 0x1C in it (gamedesc_t)
  r.nWidth = *reinterpret_cast<const WORD *>(h + 0x34);
  r.nHeight = *reinterpret_cast<const WORD *>(h + 0x36);
  r.nGameType = h[0x3C];
  r.nTileset = *reinterpret_cast<const WORD *>(h + 0x44) & 7;
  r.map = Field(h + 0x61, 0x20);
  r.type = r.nMission != 0 ? PortText("Campaign mission") : GameTypeName(r.nGameType);
  ReadPlayers(r);
  r.matchup = Matchup(r);
}

void Walk(const std::string &dir, const std::string &rel, int nDepth)
{
  if (nDepth > 4 || g_reps.size() > 2000)
    return;
  WIN32_FIND_DATAA fd;
  const HANDLE h = FindFirstFileA((dir + "*").c_str(), &fd);
  if (h == INVALID_HANDLE_VALUE)
    return;
  do {
    const std::string name = fd.cFileName;
    if (name == "." || name == "..")
      continue;
    if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
      Walk(dir + name + "\\", rel + name + "\\", nDepth + 1);
      continue;
    }
    const size_t nDot = name.rfind('.');
    if (nDot == std::string::npos || lstrcmpiA(name.c_str() + nDot, ".rep") != 0)
      continue;
    Replay r;
    r.path = dir + name;
    r.rel = rel + name;
    r.name = name.substr(0, nDot);
    r.when = fd.ftLastWriteTime;
    g_reps.push_back(r);
  } while (FindNextFileA(h, &fd));
  FindClose(h);
}

bool IsLast(const Replay &r)
{
  return lstrcmpiA(r.rel.c_str(), "LastReplay.rep") == 0;
}

// Watch plays it: a 1.17 replay of a game installed, or a Hyperion replay of this build's
// rules, data and simulation.
bool CanWatch(const Replay &r)
{
  return r.bOk && r.info.refusal.empty() && (!r.bExpansion || IsBroodwar());
}

Verdict VerdictOf(const Replay &r)
{
  Verdict v;
  if (!r.bOk) {
    v.nKind = VERDICT_BAD;
    v.head = PortText("Unreadable");
    v.line = PortText("Not a replay Hyperion reads: the file is damaged, or it is not a StarCraft replay.");
    return v;
  }
  if (r.bExpansion && !IsBroodwar()) {
    v.nKind = VERDICT_NO;
    v.head = PortText("Can\xe2\x80\x99t play in this build");
    v.line = PortText("A Brood War game, and Brood War is not installed.");
    return v;
  }
  if (!r.info.bHx) {
    v.nKind = VERDICT_OK;
    v.head = PortText("Plays in this build");
    v.line = PortText(
        "A classic replay: Hyperion plays it as the original does, command by command. It carries no state checks.");
    return v;
  }
  if (!r.info.refusal.empty()) {
    v.nKind = VERDICT_NO;
    v.head = PortText("Can\xe2\x80\x99t play in this build");
    const std::string why = r.info.refusal;
    if (why.find("revision") != std::string::npos) {
      // {0} the build that recorded it, {1} its simulation's revision, {2} this build's
      v.line = PortTextF("Another revision of Hyperion\xe2\x80\x99s simulation: recorded by {0} (simulation r{1}), and "
                         "this build plays r{2}. The game would not play out as it was played.",
                         {r.info.szBuild, std::to_string(r.info.dwSimRevision), std::to_string(HX_SIM_REVISION)});
      v.needs = PortTextF("Needs a build of simulation r{0}: {1}.",
                          {std::to_string(r.info.dwSimRevision), r.info.szBuild});
    } else if (why.find("game data") != std::string::npos) {
      v.line = PortText("Other game data: recorded with data this build\xe2\x80\x99s files do not have. The game would "
                        "not play out as it was played.");
    } else {
      // {0} why it cannot play, as the replay says it
      v.line = PortTextF("Recorded with {0}. The game would not play out as it was played.", {why});
    }
    return v;
  }
  v.nKind = VERDICT_OK;
  v.head = PortText("Plays in this build");
  // {0} the simulation's revision, {1} the rules it was played by, {2} how many state checks it carries
  v.line = PortTextF("Simulation r{0}, {1} rules, and game data as this build has it. Its {2} state checks are "
                     "compared as it plays.",
                     {std::to_string(r.info.dwSimRevision),
                      strcmp(r.info.szRule, HX_RULE_HYPERION) == 0 ? "Hyperion" : PortText("classic"),
                      Thousands(r.info.dwChecks)});
  return v;
}

bool Passes(const Replay &r)
{
  if (g_nFormat == 1 && (!r.bOk || r.info.bHx))
    return false;
  if (g_nFormat == 2 && (!r.bOk || !r.info.bHx))
    return false;
  if (g_nGame == 1 && (!r.bOk || r.bExpansion))
    return false;
  if (g_nGame == 2 && (!r.bOk || !r.bExpansion))
    return false;
  if (g_nWho > 0) {
    bool bFound = false;
    for (const Player &p : r.players)
      bFound = bFound || (p.nType == 2 && p.name == g_who[g_nWho - 1]);
    if (!bFound)
      return false;
  }
  if (!g_search.empty()) {
    std::string hay = r.name + " " + r.map;
    for (const Player &p : r.players)
      hay += " " + p.name;
    std::string needle = g_search;
    CharLowerA(&hay[0]);
    CharLowerA(&needle[0]);
    if (hay.find(needle) == std::string::npos)
      return false;
  }
  return true;
}

Replay *Picked(void)
{
  return g_nPick >= 0 && g_nPick < static_cast<int>(g_reps.size()) ? &g_reps[g_nPick] : NULL;
}

void Pick(int nRep);

// The replays the filters pass; the one picked stays picked while it is among them, else the
// first is, so what the side shows and the keys act on is always in the list.
void Filter(void)
{
  g_shown.clear();
  bool bPicked = false;
  for (size_t i = 0; i < g_reps.size(); ++i)
    if (Passes(g_reps[i])) {
      g_shown.push_back(static_cast<int>(i));
      bPicked = bPicked || static_cast<int>(i) == g_nPick;
    }
  if (!bPicked)
    Pick(g_shown.empty() ? -1 : g_shown[0]);
}

// The picked replay's map, from the map it carries.
void Pick(int nRep)
{
  if (nRep != g_nPick)
    g_fSideScroll = 0.0f;
  g_nPick = nRep;
  g_note.clear();
  Replay *r = Picked();
  if (r == NULL || g_pic.of == r->path)
    return;
  g_pic = MapPicture();
  g_pic.nSlot = 1;
  g_pic.of = r->path;
  ReplayHxInfo info;
  std::vector<unsigned char> chk;
  if (ReplayHxPeek(r->path.c_str(), &info, &chk) && !chk.empty())
    MapPictureRead(&g_pic, r->path, chk.data(), static_cast<DWORD>(chk.size()));
}

void ListReplays(void)
{
  char sz[MAX_PATH];
  sz[0] = 0;
  SFileGetUserDataPath(sz, sizeof sz, 1);
  g_root = std::string(sz) + "maps\\replays\\";
  const std::string pickWas = Picked() != NULL ? Picked()->path : g_keep;
  const std::string whoWas = g_nWho > 0 && g_nWho <= static_cast<int>(g_who.size()) ? g_who[g_nWho - 1] : "";
  g_reps.clear();
  Walk(g_root, "", 0);
  for (Replay &r : g_reps)
    ReadReplay(r);
  std::stable_sort(g_reps.begin(), g_reps.end(), [](const Replay &a, const Replay &b) {
    if (IsLast(a) != IsLast(b))
      return IsLast(a);
    return Ticks(a.when) > Ticks(b.when);
  });
  // the profiles that played them
  g_who.clear();
  for (const Replay &r : g_reps)
    for (const Player &p : r.players)
      if (p.nType == 2 && !p.name.empty() && std::find(g_who.begin(), g_who.end(), p.name) == g_who.end())
        g_who.push_back(p.name);
  // the filter follows its name, which this scan's order may have moved
  const auto who = std::find(g_who.begin(), g_who.end(), whoWas);
  g_nWho = whoWas.empty() || who == g_who.end() ? 0 : static_cast<int>(who - g_who.begin()) + 1;
  Filter();
  g_nPick = -1;
  for (size_t i = 0; i < g_reps.size(); ++i)
    if (g_reps[i].path == pickWas)
      g_nPick = static_cast<int>(i);
  if (g_nPick < 0 && !g_shown.empty())
    g_nPick = g_shown[0];
  g_pic.of.clear();
  Pick(g_nPick);
  g_bReveal = true;
}

// ---------------------------------------------------------------------------
// What can be done with the one picked.

void Watch(void)
{
  Replay *r = Picked();
  if (r == NULL || !CanWatch(*r))
    return;
  if (PortLaunchReplayFile(r->path.c_str())) {
    FrontLog("replays: watching %s\n", r->rel.c_str());
    g_nResult = FRONT_REPLAY_STARTED;
  } else {
    g_note = PortText("That replay could not be started.");
    FrontLog("replays: %s did not start\n", r->rel.c_str());
  }
}

void ShowInFolder(void)
{
  Replay *r = Picked();
  if (r == NULL)
    return;
  const std::string args = "/select,\"" + r->path + "\"";
  ShellExecuteA(NULL, "open", PortExplorer(), args.c_str(), NULL, SW_SHOWNORMAL);
}

// A file name Windows takes, of the characters a replay's name can hold.
bool NameAllowed(const std::string &s)
{
  if (s.empty() || s.size() > 60 || s.back() == '.' || s.back() == ' ')
    return false;
  for (char c : s)
    if (strchr("\\/:*?\"<>|", c) != NULL)
      return false;
  return true;
}

void Rename(void)
{
  Replay *r = Picked();
  if (r == NULL)
    return;
  if (!NameAllowed(g_newName)) {
    g_note = PortText("A replay's name cannot be empty or hold \\ / : * ? \" < > |.");
    return;
  }
  const std::string to = r->path.substr(0, r->path.size() - r->name.size() - 4) + g_newName + ".rep";
  if (lstrcmpiA(to.c_str(), r->path.c_str()) != 0 && GetFileAttributesA(to.c_str()) != INVALID_FILE_ATTRIBUTES) {
    g_note = PortText("A replay of that name is already there.");
    return;
  }
  if (!MoveFileA(r->path.c_str(), to.c_str())) {
    g_note = PortText("That replay could not be renamed.");
    return;
  }
  FrontLog("replays: %s renamed %s\n", r->rel.c_str(), g_newName.c_str());
  r->path = to;
  g_bRename = false;
  g_note.clear();
  ListReplays();
}

void Delete(void)
{
  Replay *r = Picked();
  g_bDelete = false;
  if (r == NULL)
    return;
  if (!DeleteFileA(r->path.c_str())) {
    g_note = PortText("That replay could not be deleted.");
    return;
  }
  FrontLog("replays: %s deleted\n", r->rel.c_str());
  int nNext = -1;
  for (size_t k = 0; k < g_shown.size(); ++k)
    if (g_shown[k] == g_nPick)
      nNext = k + 1 < g_shown.size() ? g_shown[k + 1] : k > 0 ? g_shown[k - 1] : -1;
  const std::string next = nNext >= 0 ? g_reps[nNext].path : std::string();
  g_nPick = -1;
  ListReplays();
  for (size_t i = 0; i < g_reps.size(); ++i)
    if (g_reps[i].path == next)
      Pick(static_cast<int>(i));
}

void Move(int nBy)
{
  if (g_shown.empty())
    return;
  int k = 0;
  for (size_t i = 0; i < g_shown.size(); ++i)
    if (g_shown[i] == g_nPick)
      k = static_cast<int>(i);
  const int nLast = static_cast<int>(g_shown.size()) - 1;
  k = k + nBy < 0 ? 0 : k + nBy > nLast ? nLast : k + nBy;
  Pick(g_shown[k]);
  g_bReveal = true;
}

// ---------------------------------------------------------------------------
// Pieces.

void Cell(float fWidth, const std::string &s, UiColor c, float fSize = 16, bool bRight = false)
{
  Decl cell;
  cell.d.layout.sizing.width = Fixed(Sc(fWidth));
  if (bRight)
    cell.d.layout.childAlignment.x = CLAY_ALIGN_X_RIGHT;
  Open(cell);
  Text(Keep(Elide(s, UI_FACE_TEXT, Fs(fSize), Sc(fWidth))), UI_FACE_TEXT, Fs(fSize), 0.0f, c);
  Close();
}

// A tab under the bar (replays.css .rp-tab): open at the foot, its number in a chip.
void DrawTab(float x, float y, float w, float h, void *pCtx)
{
  const ScDraw &d = *static_cast<const ScDraw *>(pCtx);
  const bool bOn = (d.n & 1) != 0, bHot = (d.n & 2) != 0;
  const ScLook &k = bOn ? kScTabOn : bHot ? kScTabHi : kScTab;
  const float r = Sc(4), t = fmaxf(1.0f, Sc(2));
  UiGradient(x, y, w, h, k.top, k.bot, false, r);
  UiRing(x, y, w, h, k.rim, r, t);
  if (bOn) {
    UiRing(x - Sc(4), y - Sc(4), w + Sc(8), h + Sc(4), {kScSelGlow.r, kScSelGlow.g, kScSelGlow.b, 0.35f}, Sc(6), Sc(4));
    UiFill(x, y + h - Sc(3), w, Sc(3), kScSelLine2);
  }
}

void DrawKeyChip(float x, float y, float w, float h, void *)
{
  UiRing(x, y, w, h, kScWellRim, Sc(3), fmaxf(1.0f, Sc(1)));
}

void Tabs(void)
{
  Decl row = Floating("rptabs", 0, Sc(58), Sc(118), 21, CLAY_ATTACH_POINT_LEFT_TOP, CLAY_ATTACH_POINT_LEFT_TOP);
  row.d.floating.attachTo = CLAY_ATTACH_TO_ROOT;
  row.d.layout.childGap = Gap(Fs(6));
  Open(row);
  for (int i = 0; i < 4; ++i) {
    Decl t = Box("rptab", i);
    const bool bOff = i != 0;
    const bool bHot = !bOff && Over(t.d.id);
    t.d.layout.sizing.height = Fixed(Sc(46));
    t.d.layout.padding = Pad(Fs(22), Fs(22), 0, 0);
    t.d.layout.childGap = Gap(Fs(10));
    t.d.layout.childAlignment.y = CLAY_ALIGN_Y_CENTER;
    t.d.custom.customData = Custom(DrawTab, kScTab, 0.0f, (i == 0 ? 1 : 0) | (bHot ? 2 : 0));
    Open(t);
    Decl chip;
    chip.d.layout.sizing.width = Fixed(Sc(22));
    chip.d.layout.sizing.height = Fixed(Sc(22));
    chip.d.layout.childAlignment.x = CLAY_ALIGN_X_CENTER;
    chip.d.layout.childAlignment.y = CLAY_ALIGN_Y_CENTER;
    chip.d.custom.customData = Custom(DrawKeyChip, kScTab);
    Open(chip);
    Text(Keep(std::to_string(i + 1)), UI_FACE_TEXT, Fs(13), 0.0f, bOff ? kScInkOff : kOk);
    Close();
    Text(Keep(Upper(PortText(kTabs[i]))), UI_FACE_WIDE, Fs(16), 0.05f,
         bOff ? kScInkOff : i == 0 ? kScInkHi : kScInkButton);
    Close();
    if (bOff)
      TipOn(t.d.id, PortText(kTabs[i]),
            i == 1 ? PortText("Replays others put online, which Hyperion Online will keep and check. It does not exist "
                              "yet.")
            : i == 2
                ? PortText("Hyperion Online's boards, whose games can be watched. They wait for Hyperion Online.")
                : PortText("One game after another, the camera going where the fighting is. It waits for Hyperion "
                           "Online, and for a director that reads the replay ahead of the picture."));
  }
  Close();
}

// A replay's format, as its tag before its name (replays.css .rp-fmt).
void DrawFmt(float x, float y, float w, float h, void *pCtx)
{
  const ScDraw &d = *static_cast<const ScDraw *>(pCtx);
  UiRing(x, y, w, h, d.look.rim, Sc(3), fmaxf(1.0f, Sc(1)));
}

void FmtTag(const Replay &r)
{
  const char *psz = !r.bOk ? "?" : r.info.bHx ? "HX" : "CL";
  const UiColor c = !r.bOk ? kNoInk : r.info.bHx ? kHxInk : k117Ink;
  Decl tag;
  tag.d.layout.sizing.width = Fixed(Sc(38));
  tag.d.layout.sizing.height = Fixed(Sc(20));
  tag.d.layout.childAlignment.x = CLAY_ALIGN_X_CENTER;
  tag.d.layout.childAlignment.y = CLAY_ALIGN_Y_CENTER;
  const ScLook look = {c, c, c, c, c, 3};
  tag.d.custom.customData = Custom(DrawFmt, look);
  Open(tag);
  Text(psz, UI_FACE_BOLD, Fs(11), 0.06f, c);
  Close();
}

void DrawDot(float x, float y, float w, float h, void *pCtx)
{
  const ScDraw &d = *static_cast<const ScDraw *>(pCtx);
  UiFill(x - Sc(3), y - Sc(3), w + Sc(6), h + Sc(6), {d.look.rim.r, d.look.rim.g, d.look.rim.b, 0.25f}, w);
  UiFill(x, y, w, h, d.look.rim, w / 2);
}

void Head(const char *pszTitle, const std::string &line)
{
  Decl h;
  h.d.layout.childGap = Gap(Fs(18));
  h.d.layout.childAlignment.y = CLAY_ALIGN_Y_BOTTOM;
  h.d.layout.padding = Pad(Fs(6), Fs(6), 0, Fs(12));
  Open(h);
  Text(pszTitle, UI_FACE_BOLD, Fs(27), 0.0f, kLbInkInfo);
  Text(Keep(line), UI_FACE_TEXT, Fs(16), 0.0f, kLbInkDesc);
  Close();
}

void Tools(void)
{
  Decl tools;
  tools.d.layout.sizing.width = Grow();
  tools.d.layout.padding = Pad(Fs(14), Fs(14), Fs(12), Fs(12));
  tools.d.layout.childGap = Gap(Fs(10));
  tools.d.layout.childAlignment.y = CLAY_ALIGN_Y_CENTER;
  tools.d.border = Border(kLbDivider, 0, 0, 0, Fs(2));
  Open(tools);
  Decl q = Box("rpsearch");
  q.d.layout.sizing.width = Fixed(Sc(320));
  q.d.layout.sizing.height = Fixed(Sc(36));
  if (Clicked(q.d.id))
    g_bSearch = true;
  else if (g_bPressed && !Over(q.d.id))
    g_bSearch = false;
  const std::string was = g_search;
  ScEdit(q, &g_search, 40, PortText("Search names, maps, players"), g_bSearch);
  if (g_search != was) {
    Filter();
    g_nFirst = 0;
  }
  std::vector<std::string> who(1, PortText("Every profile"));
  for (const std::string &s : g_who)
    // {0} a player's name
    who.push_back(PortTextF("{0}\xe2\x80\x99s", {s}));
  const int nWho = Pulldown("rpwho", 0, 190, who, g_nWho);
  if (nWho >= 0) {
    g_nWho = nWho;
    Filter();
    g_nFirst = 0;
  }
  const std::vector<std::string> games = {PortText("All games"), "StarCraft", "Brood War"};
  const int nGame = Pulldown("rpgame", 0, 190, games, g_nGame);
  if (nGame >= 0) {
    g_nGame = nGame;
    Filter();
    g_nFirst = 0;
  }
  const char *const apszFormats[] = {PortText(kFormats[0]), PortText(kFormats[1]), PortText(kFormats[2])};
  const int nFormat = Segments("rpfmt", apszFormats, 3, g_nFormat);
  if (nFormat >= 0) {
    g_nFormat = nFormat;
    Filter();
    g_nFirst = 0;
  }
  Close();
}

void List(void)
{
  Decl box = Box("rplist");
  box.d.layout.layoutDirection = CLAY_TOP_TO_BOTTOM;
  box.d.layout.sizing.width = Grow();
  box.d.layout.sizing.height = Grow();
  box.d.clip.vertical = true;
  InnerBox(box);
  Open(box);
  Tools();

  Decl head;
  head.d.layout.sizing.width = Grow();
  head.d.layout.sizing.height = Fixed(Sc(36));
  head.d.layout.padding = Pad(Fs(14), Fs(14), 0, 0);
  head.d.layout.childGap = Gap(Fs(12));
  head.d.layout.childAlignment.y = CLAY_ALIGN_Y_CENTER;
  head.d.backgroundColor = Col(kScMenuFill);
  Open(head);
  Decl hn;
  hn.d.layout.sizing.width = Grow();
  Open(hn);
  Text(PortText("REPLAY"), UI_FACE_WIDE, Fs(12), 0.05f, kScInkHead);
  Close();
  const char *const kHeads[] = {PortText("MAP"), PortText("PLAYERS"), PortText("LENGTH"), PortText("PLAYED"), ""};
  for (int c = 0; c < 5; ++c) {
    Decl h;
    h.d.layout.sizing.width = Fixed(Sc(kCols[c]));
    if (c == 2)
      h.d.layout.childAlignment.x = CLAY_ALIGN_X_RIGHT;
    Open(h);
    Text(kHeads[c], UI_FACE_WIDE, Fs(12), 0.05f, kScInkHead);
    Close();
  }
  Close();

  const int nMaxFirst = static_cast<int>(g_shown.size()) > g_nShownRows ? static_cast<int>(g_shown.size()) - g_nShownRows : 0;
  g_nFirst += WheelRows(Over(Id("rplist")));
  if (g_bReveal) {
    g_bReveal = false;
    for (size_t k = 0; k < g_shown.size(); ++k)
      if (g_shown[k] == g_nPick && (static_cast<int>(k) < g_nFirst || static_cast<int>(k) >= g_nFirst + g_nShownRows))
        g_nFirst = static_cast<int>(k) < g_nFirst ? static_cast<int>(k) : static_cast<int>(k) - g_nShownRows + 1;
  }
  g_nFirst = g_nFirst < 0 ? 0 : g_nFirst > nMaxFirst ? nMaxFirst : g_nFirst;

  const float fRow = Sc(58);
  // the name column's room, from the list's width in the last frame
  float fNameW = Sc(400);
  const Clay_ElementData was = Clay_GetElementData(Id("rplist"));
  if (was.found) {
    // the rows' padding and gaps as Pad and Gap make them (UiPx of Fs's units)
    fNameW = was.boundingBox.width - 2 * UiPx(Fs(14)) - 5 * UiPx(Fs(12)) - Sc(4);
    for (float c : kCols)
      fNameW -= Sc(c);
  }
  for (int k = g_nFirst; k < static_cast<int>(g_shown.size()) && k < g_nFirst + g_nShownRows; ++k) {
    const int i = g_shown[k];
    const Replay &r = g_reps[i];
    Decl row = Box("rprow", i);
    const bool bPick = i == g_nPick;
    const bool bHot = Over(row.d.id);
    if (Clicked(row.d.id)) {
      Pick(i);
      if (DoubleClicked(row.d.id))
        Watch();
    }
    row.d.layout.sizing.width = Grow();
    row.d.layout.sizing.height = Fixed(fRow);
    row.d.layout.padding = Pad(Fs(14), Fs(14), 0, 0);
    row.d.layout.childGap = Gap(Fs(12));
    row.d.layout.childAlignment.y = CLAY_ALIGN_Y_CENTER;
    row.d.border = Border(bPick ? kScSelLine2 : kLbDivider, bPick ? Fs(3) : 0, 0, 0, 1);
    if (bPick)
      row.d.backgroundColor = Col(kLbItemHi);
    else if (bHot)
      row.d.backgroundColor = Col(kLbItem);
    Open(row);
    const bool bGood = CanWatch(r);
    Decl name;
    name.d.layout.layoutDirection = CLAY_TOP_TO_BOTTOM;
    name.d.layout.sizing.width = Grow();
    name.d.layout.childGap = Gap(1);
    Open(name);
    Decl line;
    line.d.layout.childGap = Gap(Fs(8));
    line.d.layout.childAlignment.y = CLAY_ALIGN_Y_CENTER;
    Open(line);
    FmtTag(r);
    Text(Keep(Elide(r.name, UI_FACE_BOLD, Fs(18), fNameW - Sc(46))), UI_FACE_BOLD, Fs(18), 0.0f,
         !bGood ? kLbInkDesc : bPick || bHot ? kScInkHi : kLbInkInfo);
    Close();
    std::string under = r.rel;
    if (r.bOk && !r.bExpansion)
      under += std::string(kDot) + "StarCraft";
    Text(Keep(Elide(under, UI_FACE_TEXT, Fs(13), fNameW)), UI_FACE_TEXT, Fs(13), 0.0f, kScInkOff);
    Close();
    Cell(kCols[0], r.bOk ? r.map : "\xe2\x80\x94", kLbInkDesc);
    Cell(kCols[1], r.bOk ? r.matchup : "\xe2\x80\x94", kLbInkDesc);
    Cell(kCols[2], r.bOk ? Clock(r.dwFrames, r.nSpeed) : "\xe2\x80\x94", kLbInkInfo, 16, true);
    Cell(kCols[3], Age(r.when), kLbInkDesc);
    Decl st;
    st.d.layout.sizing.width = Fixed(Sc(kCols[4]));
    st.d.layout.childAlignment.x = CLAY_ALIGN_X_RIGHT;
    st.d.layout.childAlignment.y = CLAY_ALIGN_Y_CENTER;
    Open(st);
    if (!bGood) {
      Decl dot;
      dot.d.layout.sizing.width = Fixed(Sc(10));
      dot.d.layout.sizing.height = Fixed(Sc(10));
      const ScLook look = {kNoInk, kNoInk, kNoInk, kNoInk, kNoInk, 0};
      dot.d.custom.customData = Custom(DrawDot, look);
      Open(dot);
      Close();
    }
    Close();
    Close();
  }
  if (g_shown.empty()) {
    Decl none;
    none.d.layout.padding = Pad(Fs(30), Fs(30), Fs(30), Fs(30));
    Open(none);
    Text(g_reps.empty()
             ? PortText("No replays yet. Each game you play leaves one here, as LastReplay and in AutoSave.")
             : PortText("No replay matches."),
         UI_FACE_TEXT, Fs(17), 0.0f, kLbInkDesc);
    Close();
  }
  Close();

  Clay_ElementData data = Clay_GetElementData(Id("rplist"));
  if (data.found) {
    const int n = static_cast<int>((data.boundingBox.height - Sc(36) - Sc(64)) / fRow);
    g_nShownRows = n > 1 ? n : 1;
  }
}

void PlayerRow(const Player &p)
{
  Decl row;
  row.d.layout.sizing.width = Grow();
  row.d.layout.sizing.height = Fixed(Sc(32));
  row.d.layout.childGap = Gap(Fs(10));
  row.d.layout.childAlignment.y = CLAY_ALIGN_Y_CENTER;
  Open(row);
  Chip(p.nColour >= 0 ? kSlotColours[p.nColour] : 0xb4b4b4, 14);
  Decl nm;
  nm.d.layout.sizing.width = Grow();
  nm.d.clip.horizontal = true;
  Open(nm);
  Text(Keep(p.name), UI_FACE_BOLD, Fs(15), 0.0f, kScInkHi);
  Close();
  Decl race;
  race.d.layout.sizing.width = Fixed(Sc(80));
  race.d.layout.childAlignment.x = CLAY_ALIGN_X_RIGHT;
  Open(race);
  Text(p.nRace >= 0 ? PortText(kRaceNames[p.nRace]) : PortText("Random"), UI_FACE_TEXT, Fs(15), 0.0f,
       p.nRace >= 0 ? kRaceInk[p.nRace] : kLbInkDesc);
  Close();
  Cell(90, p.nType == 1 ? PortText("Computer") : PortText("Player"), kLbInkDesc, 15, true);
  Close();
}

void Fact(const char *pszKey, const std::string &value, bool bOff = false)
{
  Decl f;
  f.d.layout.sizing.width = Grow();
  f.d.layout.childGap = Gap(Fs(10));
  Open(f);
  Decl k;
  k.d.layout.sizing.width = Fixed(Sc(120));
  Open(k);
  Text(pszKey, UI_FACE_TEXT, Fs(14), 0.03f, kScInkHead);
  Close();
  Decl v;
  v.d.layout.sizing.width = Grow();
  Open(v);
  Text(Keep(value), UI_FACE_TEXT, Fs(14), 0.0f, bOff ? kNoInk : kLbInkInfo, -1, 1.3f);
  Close();
  Close();
}

void Side(void)
{
  Decl side = Box("rpside");
  side.d.layout.layoutDirection = CLAY_TOP_TO_BOTTOM;
  side.d.layout.sizing.width = Fixed(Sc(kSide));
  side.d.layout.sizing.height = Grow();
  InnerBox(side);
  Open(side);
  // what the side holds scrolls under the wheel, when it is more than fits
  g_fSideScroll += WheelPixels(Over(Id("rpsidebody")), Sc(60));
  g_fSideScroll = g_fSideScroll > g_fSideMax ? g_fSideMax : g_fSideScroll < 0 ? 0 : g_fSideScroll;
  Decl body = Box("rpsidebody");
  body.d.layout.sizing.width = Grow();
  body.d.layout.sizing.height = Grow();
  body.d.clip.vertical = true;
  body.d.clip.childOffset.y = -g_fSideScroll;
  Open(body);
  Decl inner = Box("rpsideinner");
  inner.d.layout.layoutDirection = CLAY_TOP_TO_BOTTOM;
  inner.d.layout.sizing.width = Grow();
  inner.d.layout.padding = Pad(Fs(18), Fs(18), Fs(16), Fs(12));
  inner.d.layout.childGap = Gap(Fs(4));
  Open(inner);
  const Replay *r = Picked();
  if (r == NULL) {
    Text(g_reps.empty() ? PortText("Nothing to show.") : PortText("Pick a replay."), UI_FACE_TEXT, Fs(16), 0.0f,
         kLbInkDesc);
    Close();
    Close();
    Close();
    return;
  }
  const Verdict v = VerdictOf(*r);
  if (r->bOk) {
    // the picture, at most 260 units high in the map's shape
    const float fH = 260, fW = r->nWidth > 0 && r->nHeight > 0 ? fminf(kSide - 40, fH * r->nWidth / r->nHeight) : fH;
    Decl at;
    at.d.layout.sizing.width = Grow();
    at.d.layout.childAlignment.x = CLAY_ALIGN_X_CENTER;
    at.d.layout.padding = Pad(0, 0, 0, Fs(10));
    Open(at);
    MapPreview("rppic", g_pic.of == r->path && g_pic.w > 0 ? &g_pic : NULL, r->nTileset, r->map.c_str(), r->nWidth,
               r->nHeight, fW, fH, 30, false, r->nGameType == 0x0A);
    Close();
  }
  Text(Keep(r->name), UI_FACE_BOLD, Fs(24), 0.0f, kLbInkInfo);
  std::string sub = r->bOk ? r->map + kDot + Clock(r->dwFrames, r->nSpeed) + kDot : std::string();
  sub += Age(r->when);
  Text(Keep(sub), UI_FACE_TEXT, Fs(15), 0.0f, kLbInkDesc);
  // the verdict (replays.css .rp-verdict)
  Decl vb;
  vb.d.layout.layoutDirection = CLAY_TOP_TO_BOTTOM;
  vb.d.layout.sizing.width = Grow();
  vb.d.layout.padding = Pad(Fs(14), Fs(14), Fs(10), Fs(10));
  vb.d.layout.childGap = Gap(Fs(4));
  const bool bOkV = v.nKind == VERDICT_OK;
  vb.d.backgroundColor = Col(bOkV ? kOkFill : kNoFill);
  vb.d.border = Border(bOkV ? kOkInk : kNoInk, Fs(4), 0, 0, 0);
  Radius(vb, 3);
  Decl gap;
  gap.d.layout.sizing.height = Fixed(Sc(10));
  Open(gap);
  Close();
  Open(vb);
  Text(Keep(v.head), UI_FACE_BOLD, Fs(17), 0.0f, bOkV ? kOkInk : kNoInk);
  Text(Keep(v.line), UI_FACE_TEXT, Fs(15), 0.0f, kLbInkInfo, -1, 1.35f);
  if (!v.needs.empty())
    Text(Keep(v.needs), UI_FACE_TEXT, Fs(15), 0.0f, kScInkHi, -1, 1.35f);
  Close();
  if (r->bOk) {
    Decl gap2;
    gap2.d.layout.sizing.height = Fixed(Sc(8));
    Open(gap2);
    Close();
    const size_t nShow = r->players.size() > 8 ? 6 : r->players.size();
    for (size_t i = 0; i < nShow; ++i)
      PlayerRow(r->players[i]);
    if (nShow < r->players.size()) {
      size_t nCpu = 0;
      for (size_t i = nShow; i < r->players.size(); ++i)
        nCpu += r->players[i].nType == 1;
      // {0} how many players are not shown, {1} how many of them are computers
      Text(Keep(PortTextNF("and {0} more: {1} computer", "and {0} more: {1} computers",
                           static_cast<long long>(nCpu),
                           {std::to_string(r->players.size() - nShow), std::to_string(nCpu)})),
           UI_FACE_TEXT, Fs(14), 0.0f, kScInkOff);
    }
    Decl gap3;
    gap3.d.layout.sizing.height = Fixed(Sc(10));
    Open(gap3);
    Close();
    Fact(PortText("Format"), r->info.bHx ? PortText("Hyperion replay (hxRS)")
                                         : PortText("Classic replay (reRS), byte for byte as the original writes it"));
    Fact(PortText("Game"), std::string(r->bExpansion ? "Brood War" : "StarCraft") + kDot + r->type);
    // {0} the map's name, {1} and {2} its size in tiles
    Fact(PortText("Map"),
         PortTextF("{0}, {1} \xc3\x97 {2} tiles",
                   {r->map, std::to_string(r->nWidth), std::to_string(r->nHeight)}) +
             kDot + PortTextNF("{0} player", "{0} players", static_cast<long long>(r->players.size()),
                               {std::to_string(r->players.size())}));
    // {0} how many frames it is, {1} the speed it was recorded at
    Fact(PortText("Length"),
         Clock(r->dwFrames, r->nSpeed) + kDot +
             PortTextF("{0} frames at {1}",
                       {Thousands(r->dwFrames),
                        PortText(kSpeedNames[r->nSpeed >= 0 && r->nSpeed < kPortSpeeds ? r->nSpeed : kPortSpeeds - 1])}));
    if (r->info.bHx && r->info.bRule) {
      Fact(PortText("Rules"), strcmp(r->info.szRule, HX_RULE_HYPERION) == 0 ? "Hyperion" : PortText("Classic"));
      const bool bRev = r->info.dwSimRevision == HX_SIM_REVISION;
      const std::string rev = std::to_string(r->info.dwSimRevision);
      // {0} the replay's simulation revision, {1} this build's
      Fact(PortText("Simulation"),
           bRev ? PortTextF("r{0}, as this build", {rev})
                : PortTextF("r{0}, this build is r{1}", {rev, std::to_string(HX_SIM_REVISION)}),
           !bRev);
      if (r->info.szMod[0])
        // {0} the mod, {1} the mods it is built on
        Fact(PortText("Mod"), r->info.szDepends[0]
                                  ? PortTextF("{0}, with {1}", {r->info.szMod, r->info.szDepends})
                                  : std::string(r->info.szMod));
      Fact(PortText("State checks"), PortTextF("{0}, one a second of game time", {Thousands(r->info.dwChecks)}));
      Fact(PortText("Recorded by"), r->info.szBuild);
    } else if (!r->info.bHx) {
      Fact(PortText("State checks"), PortText("None: the classic format has no room for them"));
    }
    Fact(PortText("File"), r->rel + kDot + FileDate(r->when));
  }
  Close();
  Close();
  const Clay_ElementData in = Clay_GetElementData(Id("rpsideinner"));
  const Clay_ElementData out = Clay_GetElementData(Id("rpsidebody"));
  g_fSideMax = in.found && out.found && in.boundingBox.height > out.boundingBox.height
                   ? in.boundingBox.height - out.boundingBox.height
                   : 0.0f;
  // what can be done: Watch, and Submit Online, Rename, Show in Folder, Delete
  Decl acts;
  acts.d.layout.layoutDirection = CLAY_TOP_TO_BOTTOM;
  acts.d.layout.sizing.width = Grow();
  acts.d.layout.padding = Pad(Fs(14), Fs(14), Fs(10), Fs(16));
  acts.d.layout.childGap = Gap(Fs(10));
  acts.d.border = Border(kLbDivider, 0, 0, Fs(2), 0);
  Open(acts);
  if (!g_note.empty())
    Text(Keep(g_note), UI_FACE_TEXT, Fs(15), 0.0f, kScInkAccept);
  Decl w = Box("rpwatch");
  w.d.layout.sizing.width = Grow();
  w.d.layout.sizing.height = Fixed(Sc(52));
  if (ScButton(w, PortText("Watch Replay"), 'W', true, CanWatch(*r)))
    Watch();
  if (!CanWatch(*r))
    TipOn(Id("rpwatch"), v.head.c_str(), (v.needs.empty() ? v.line : v.needs).c_str());
  Decl row;
  row.d.layout.sizing.width = Grow();
  row.d.layout.childGap = Gap(Fs(4));
  Open(row);
  Decl sub2 = Box("rpsubmit");
  sub2.d.layout.sizing.height = Fixed(Sc(32));
  ScMini(sub2, PortText("Submit"), 'U', false);
  TipOn(Id("rpsubmit"), PortText("Submit Online"),
        PortText("Puts the replay on Hyperion Online, which plays it to its end with no screen and checks each state "
                 "check. It waits for Hyperion Online."));
  Decl rn = Box("rprename");
  rn.d.layout.sizing.height = Fixed(Sc(32));
  if (ScMini(rn, PortText("Rename"), 'N')) {
    g_bRename = true;
    g_newName = r->name;
  }
  Decl sf = Box("rpfolder");
  sf.d.layout.sizing.height = Fixed(Sc(32));
  if (ScMini(sf, PortText("Show in Folder"), 'F'))
    ShowInFolder();
  Decl del = Box("rpdelete");
  del.d.layout.sizing.height = Fixed(Sc(32));
  if (ScMini(del, PortText("Delete"), 0))
    g_bDelete = true;
  TipOn(Id("rpdelete"), PortText("Delete"), PortText("Deletes the file, after asking. Hotkey: Delete"));
  Close();
  Close();
  Close();
}

void RenameDialog(void)
{
  const bool bOut = DialogOpen("rprenamedlg", 760, 260, 300);
  DialogTitle(PortText("RENAME REPLAY"));
  Decl e = Box("rprenameedit");
  e.d.layout.sizing.width = Grow();
  e.d.layout.sizing.height = Fixed(Sc(44));
  ScEdit(e, &g_newName, 60, PortText("The replay's name"), true);
  if (!g_note.empty())
    Text(Keep(g_note), UI_FACE_TEXT, Fs(15), 0.0f, kScInkAccept);
  Decl row;
  row.d.layout.sizing.width = Grow();
  row.d.layout.childGap = Gap(Fs(12));
  row.d.layout.childAlignment.x = CLAY_ALIGN_X_CENTER;
  Open(row);
  Decl ok = Box("rprenameok");
  ok.d.layout.sizing.width = Fixed(Sc(220));
  ok.d.layout.sizing.height = Fixed(Sc(52));
  const bool bOk = ScButton(ok, PortText("Rename"), 0, true);
  Decl no = Box("rprenameno");
  no.d.layout.sizing.width = Fixed(Sc(220));
  no.d.layout.sizing.height = Fixed(Sc(52));
  const bool bNo = ScButton(no, PortText("Cancel"), 0, false);
  Close();
  Close();
  if (bOk)
    Rename();
  else if (bNo || bOut) {
    g_bRename = false;
    g_note.clear();
  }
}

}  // namespace

namespace FrontKit {

// ---------------------------------------------------------------------------
// The replays screen.

void ReplaysOpen(void)
{
  g_nResult = 0;
  g_note.clear();
  g_lbOpen.clear();
  g_bRename = g_bDelete = g_bSearch = false;
  ListReplays();
  size_t nHx = 0, nBad = 0;
  for (const Replay &r : g_reps) {
    nHx += r.bOk && r.info.bHx;
    nBad += !r.bOk;
  }
  FrontLog("replays: %u in %s (%u Hyperion, %u unreadable)\n", static_cast<unsigned>(g_reps.size()), g_root.c_str(),
           static_cast<unsigned>(nHx), static_cast<unsigned>(nBad));
}

void ReplaysClose(void)
{
  g_keep = Picked() != NULL ? Picked()->path : std::string();
  g_nPick = -1;
  g_reps.clear();
  g_shown.clear();
  g_pic = MapPicture();
}

int ReplaysResult(void)
{
  return g_nResult;
}

void ReplaysScreen(void)
{
  MapPreviewFrame();
  Tabs();
  Decl panel = Box("rppanel");
  panel.d.layout.layoutDirection = CLAY_TOP_TO_BOTTOM;
  panel.d.layout.sizing.width = Fixed(UiWidth() - 2 * Sc(58));
  panel.d.layout.sizing.height = Fixed(fminf(Sc(950), UiHeight() - Sc(kTop) - Sc(74)));
  panel.d.layout.padding = Pad(Fs(16), Fs(16), Fs(18), Fs(16));
  panel.d.floating.attachTo = CLAY_ATTACH_TO_ROOT;
  panel.d.floating.offset.x = Sc(58);
  panel.d.floating.offset.y = Sc(kTop);
  panel.d.floating.zIndex = 20;
  static UiCustom frame = {0.0f, DrawScFrame, NULL};
  panel.d.custom.customData = &frame;
  Open(panel);
  // {0} the profile playing
  Head(PortText("My Replays"),
       std::string(PortText("replays\\ in Hyperion\xe2\x80\x99s data folder")) + kDot +
           PortTextNF("{0} replay", "{0} replays", static_cast<long long>(g_reps.size()),
                      {std::to_string(g_reps.size())}) +
           kDot +
           (gLocalPlayerName[0] ? PortTextF("{0} is playing", {gLocalPlayerName})
                                : std::string(PortText("no profile yet"))));
  Decl body;
  body.d.layout.sizing.width = Grow();
  body.d.layout.sizing.height = Grow();
  body.d.layout.childGap = Gap(Fs(14));
  Open(body);
  List();
  Side();
  Close();
  Close();
  if (g_bRename)
    RenameDialog();
  if (g_bDelete) {
    const Replay *r = Picked();
    const char *const kButtons[] = {PortText("Delete"), PortText("Cancel")};
    // {0} the replay's name, then the file deleted
    const int n = r == NULL ? 1
                            : FrontDialog(Keep(PortTextF("Delete {0}?", {r->name})),
                                          Keep(PortTextF("The file {0} is deleted.", {r->rel})), kButtons, "DC", 2);
    if (n == 0)
      Delete();
    else if (n == 1)
      g_bDelete = false;
  }
  DrawTip();
  PulldownCloseOutside();
}

bool ReplaysKeys(void)
{
  if (g_bRename) {
    if (Key(VK_RETURN))
      Rename();
    else if (Key(VK_ESCAPE)) {
      g_bRename = false;
      g_note.clear();
    }
    return true;
  }
  if (g_bDelete) {
    if (Key(VK_ESCAPE))
      g_bDelete = false;
    return true;
  }
  if (PulldownKeys())
    return true;
  if (g_bSearch) {
    if (Key(VK_ESCAPE) || Key(VK_RETURN))
      g_bSearch = false;
    else if (Key(VK_DOWN))
      Move(1);
    else if (Key(VK_UP))
      Move(-1);
    return true;
  }
  if (Key(VK_DOWN))
    Move(1);
  else if (Key(VK_UP))
    Move(-1);
  else if (Key(VK_NEXT))
    Move(g_nShownRows);
  else if (Key(VK_PRIOR))
    Move(-g_nShownRows);
  else if (Key('W') || Key(VK_RETURN))
    Watch();
  else if (Key('N') && Picked() != NULL) {
    g_bRename = true;
    g_newName = Picked()->name;
    g_typed.clear();  // the N is not the name's
  } else if (Key('F'))
    ShowInFolder();
  else if (Key(VK_DELETE) && Picked() != NULL)
    g_bDelete = true;
  else if (Key(VK_ESCAPE) || Key('B'))
    g_nResult = FRONT_REPLAYS_HOME;
  return true;
}

}  // namespace FrontKit
