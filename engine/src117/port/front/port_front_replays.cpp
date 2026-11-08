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
#include <map>
#include <set>
#include <string>
#include <vector>
#include "../gameplay/port_launch.h"  // PortLaunchReplayFile
#include "../platform/port_paths.h"   // PortExplorer
#include "../platform/port_settings.h"
#include "oc_data.h"           // OC_VA, where 1.17's data is
#include "Storm.h"             // SStrCopy
#include "grid.h"              // IsBroodwar
#include "../../h/hxsim.h"        // HX_SIM_REVISION
#include "../../h/hxrules.h"      // HX_RULE_HYPERION
#include "../../h/port_hxreplay.h"  // ReplayHxPeek
#include "../../h/port_replaycatalog.h"
#include "../../h/port_replaykraken.h"
#include "../../h/port_replaylibrary.h"
#include "../../h/port_replayonline.h"
#include "port_frontkit.h"
#include "port_frontlobby.h"
#include "port_frontscreens.h"
#include "port_ui.h"
#include "../data/port_stormapi.h"  // SFileGetUserDataPath
#include "../data/port_moddata.h"
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

enum { VERDICT_OK, VERDICT_NO, VERDICT_BAD, VERDICT_WAIT };

struct Verdict {
  int nKind;
  std::string head, line, needs;
};

using Replay = ReplayCatalog::Entry;
using Player = ReplayCatalog::Player;

std::string g_root;                 // maps\replays in the data folder
std::string g_keep;                 // the replay path picked when the screen closed
std::string g_keepContent;          // and its rename-stable identity
const std::vector<Replay> &g_reps = ReplayCatalog::Entries();
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
std::string g_picContent;
bool g_bRename;
std::string g_newName;
bool g_bDelete;
float g_fSideScroll;                // the side, scrolled down this far
float g_fSideMax;                   // as far as it scrolls, from the last frame
int g_nTab = 0;                     // 0 Local, 1 Online
ReplayOnline::State g_online;
int g_nOnlinePick = -1;
int g_nOnlineFirst = 0;
std::vector<int> g_onlineShown;
ReplayKraken::Query g_query;
std::string g_onlineSearch;
bool g_bOnlineSearch;
bool g_bOnlinePlayerEdit, g_bOnlineMatchupEdit;
ReplayLibrary::Data g_library;
std::string g_activeCollection;
std::string g_activeQuery;
std::string g_savedServerApi;
std::string g_selectAfterDownload;
bool g_bFavouriteOnly, g_bStartedOnly, g_bNotesOnly;
bool g_bNoteDialog, g_bViewDialog, g_bExportDialog, g_bImportDialog;
bool g_bNoteTagsEdit;
std::string g_editNote, g_editTags, g_editName, g_editPath;
bool g_editShared;
std::string g_editNoteId;
std::string g_noteTarget;
bool g_exportFavourites, g_exportNotes, g_exportQueries, g_exportCollections;
std::map<std::string, ReplayHxInfo> g_downloadInfo;
std::set<std::string> g_handledDownloads;
std::set<std::string> g_validationPosted;
std::string g_onlineKeep;
const void *g_onlinePageData = nullptr;
size_t g_onlinePageSize = 0;

// ---------------------------------------------------------------------------
// Reading the folder.

ULONGLONG Ticks(const FILETIME &ft)
{
  return static_cast<ULONGLONG>(ft.dwHighDateTime) << 32 | ft.dwLowDateTime;
}

FILETIME FileTimeOf(uint64_t ticks)
{
  FILETIME out;
  out.dwLowDateTime = static_cast<DWORD>(ticks);
  out.dwHighDateTime = static_cast<DWORD>(ticks >> 32);
  return out;
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

std::string Age(uint64_t when)
{
  FILETIME now;
  GetSystemTimeAsFileTime(&now);
  const double h = Ticks(now) > when ? (Ticks(now) - when) / 36e9 : 0.0;
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
  const FILETIME ft = FileTimeOf(when);
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

bool IsLast(const Replay &r)
{
  return lstrcmpiA(r.rel.c_str(), "LastReplay.rep") == 0;
}

// Watch plays it: a 1.17 replay of a game installed, or a Hyperion replay of this build's
// rules, data and simulation.
bool CanWatch(const Replay &r)
{
  return !r.bPending && r.bOk && r.info.refusal.empty() && (!r.bExpansion || IsBroodwar()) &&
         (r.info.bHx || ModDataId()[0] == 0);  // A classic header cannot prove modified-data compatibility.
}

Verdict VerdictOf(const Replay &r)
{
  Verdict v;
  if (r.bPending) {
    v.nKind = VERDICT_WAIT;
    v.head = PortText(r.bDeferred ? "Waiting for the writer" : "Reading replay");
    v.line = r.bDeferred ? PortText("This file changed recently. It will be inspected after it stops being written.")
                         : PortText("The incremental catalog has not inspected this file yet.");
    return v;
  }
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
  if (!r.info.bHx && ModDataId()[0]) {
    v.nKind = VERDICT_NO;
    v.head = PortText("Can\xe2\x80\x99t verify with this mod");
    v.line = PortText("A classic replay has no Hyperion data fingerprint. Disable the mod to check playback safely.");
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

const ReplayLibrary::Record *LibraryRecord(const std::string &id)
{
  return id.empty() ? nullptr : ReplayLibrary::Find(g_library, id);
}

bool IsDownloaded(const Replay &r)
{
  if (r.rel.find("Replay Kraken/") == 0 || r.rel.find("Replay Kraken\\") == 0)
    return true;
  const ReplayLibrary::Record *record = LibraryRecord(r.contentId);
  return record && !record->sources.empty();
}

bool Passes(const Replay &r)
{
  const ReplayLibrary::Record *record = LibraryRecord(r.contentId);
  if (!g_activeCollection.empty()) {
    const auto list = std::find_if(g_library.collections.begin(), g_library.collections.end(), [](const auto &candidate) {
      return candidate.id == g_activeCollection;
    });
    if (list == g_library.collections.end() || !ReplayLibrary::InCollection(*list, r.contentId))
      return false;
  }
  if (g_bFavouriteOnly && (!record || !record->bFavourite))
    return false;
  if (g_bStartedOnly && (!record || !record->startedAt))
    return false;
  if (g_bNotesOnly && !ReplayLibrary::HasNotes(g_library, r.contentId))
    return false;
  if (!g_query.matchup.empty() && ReplayKraken::MatchupKey(r.matchup) != ReplayKraken::MatchupKey(g_query.matchup))
    return false;
  if (!g_query.race.empty()) {
    const std::string race = PortLower(g_query.race);
    const bool bFound = std::any_of(r.players.begin(), r.players.end(), [&](const Player &player) {
      return player.nRace >= 0 && player.nRace < 3 && PortLower(PortText(kRaceNames[player.nRace])) == race;
    });
    if (!bFound)
      return false;
  }
  if (!g_query.result.empty() && PortLower(g_query.result) != "unknown")
    return false;  // Hyperion's local replay headers do not carry authoritative results.
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

const Replay *Picked(void)
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
  const Replay *r = Picked();
  if (r == NULL || (g_pic.of == r->path && g_picContent == r->contentId))
    return;
  g_pic = MapPicture();
  g_picContent = r->contentId;
  if (r->bPending)
    return;
  g_pic.nSlot = 1;
  g_pic.of = r->path;
  ReplayHxInfo info;
  std::vector<unsigned char> chk;
  if (ReplayHxPeek(r->path.c_str(), &info, &chk) && !chk.empty())
    MapPictureRead(&g_pic, r->path, chk.data(), static_cast<DWORD>(chk.size()));
}

void RebuildWho(const std::string &whoWas)
{
  g_who.clear();
  for (const Replay &r : g_reps)
    for (const Player &p : r.players)
      if (p.nType == 2 && !p.name.empty() && std::find(g_who.begin(), g_who.end(), p.name) == g_who.end())
        g_who.push_back(p.name);
  const auto who = std::find(g_who.begin(), g_who.end(), whoWas);
  g_nWho = whoWas.empty() || who == g_who.end() ? 0 : static_cast<int>(who - g_who.begin()) + 1;
}

void RestoreSelection(const std::string &path, const std::string &content)
{
  g_nPick = -1;
  for (size_t i = 0; i < g_reps.size(); ++i)
    if ((!path.empty() && g_reps[i].path == path) || (!content.empty() && g_reps[i].contentId == content)) {
      g_nPick = static_cast<int>(i);
      break;
    }
  Filter();
  g_bReveal = true;
}

void SyncCatalog(void)
{
  const std::string path = Picked() ? Picked()->path : g_keep;
  const std::string content = Picked() ? Picked()->contentId : g_keepContent;
  const std::string whoWas = g_nWho > 0 && g_nWho <= static_cast<int>(g_who.size()) ? g_who[g_nWho - 1] : "";
  RebuildWho(whoWas);
  RestoreSelection(path, content);
}

void ListReplays(void)
{
  char sz[MAX_PATH];
  sz[0] = 0;
  SFileGetUserDataPath(sz, sizeof sz, 1);
  g_root = std::string(sz) + "maps\\replays\\";
  ReplayCatalog::Open(g_root);
  SyncCatalog();
}

// ---------------------------------------------------------------------------
// What can be done with the one picked.

bool WatchPath(const std::string &path, const std::string &contentId, const std::string &shownName)
{
  if (!PortLaunchReplayFile(path.c_str())) {
    g_note = PortText("That replay could not be started.");
    FrontLog("replays: %s did not start\n", shownName.c_str());
    return false;
  }
  FrontLog("replays: watching %s\n", shownName.c_str());
  if (!contentId.empty()) {
    ReplayLibrary::MarkStarted(&g_library, contentId, ReplayLibrary::NowSeconds());
    ReplayOnline::SaveLibrary(g_library);  // Started means accepted, not a claimed full watch.
  }
  g_nResult = FRONT_REPLAY_STARTED;
  return true;
}

void Watch(void)
{
  const Replay *r = Picked();
  if (r == NULL || !CanWatch(*r))
    return;
  WatchPath(r->path, r->contentId, r->rel);
}

void ShowInFolder(void)
{
  const Replay *r = Picked();
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
  const Replay *r = Picked();
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
  const std::string oldPath = r->path;
  ReplayCatalog::Renamed(oldPath, to);
  ReplayOnline::LocalRenamed(oldPath, to);
  g_keep = to;
  g_keepContent = r->contentId;
  g_bRename = false;
  g_note.clear();
  ListReplays();
}

void Delete(void)
{
  const Replay *r = Picked();
  g_bDelete = false;
  if (r == NULL)
    return;
  if (!DeleteFileA(r->path.c_str())) {
    g_note = PortText("That replay could not be deleted.");
    return;
  }
  FrontLog("replays: %s deleted\n", r->rel.c_str());
  const std::string deletedPath = r->path;
  int nNext = -1;
  for (size_t k = 0; k < g_shown.size(); ++k)
    if (g_shown[k] == g_nPick)
      nNext = k + 1 < g_shown.size() ? g_shown[k + 1] : k > 0 ? g_shown[k - 1] : -1;
  const std::string next = nNext >= 0 ? g_reps[nNext].path : std::string();
  ReplayCatalog::Deleted(deletedPath);
  ReplayOnline::LocalDeleted(deletedPath);
  g_nPick = -1;
  g_keep = next;
  g_keepContent.clear();
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
    const bool bOff = i >= 2;
    const bool bHot = !bOff && Over(t.d.id);
    if (!bOff && Clicked(t.d.id) && g_nTab != i) {
      g_nTab = i;
      g_note.clear();
      if (i == 1)
        ReplayOnline::Fetch(g_query, true);
      else
        ReplayCatalog::Refresh();
    }
    t.d.layout.sizing.height = Fixed(Sc(46));
    t.d.layout.padding = Pad(Fs(22), Fs(22), 0, 0);
    t.d.layout.childGap = Gap(Fs(10));
    t.d.layout.childAlignment.y = CLAY_ALIGN_Y_CENTER;
    t.d.custom.customData = Custom(DrawTab, kScTab, 0.0f, (i == g_nTab ? 1 : 0) | (bHot ? 2 : 0));
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
         bOff ? kScInkOff : i == g_nTab ? kScInkHi : kScInkButton);
    Close();
    if (bOff)
      TipOn(t.d.id, PortText(kTabs[i]),
            i == 2 ? PortText("Leaderboards are outside this Replay Kraken workspace; the server has no native rating contract.")
                   : PortText("Continuous online Replay TV is outside this workspace. The in-playback director remains available."));
    else if (i == 1)
      TipOn(t.d.id, PortText("Online"), PortText("Browse, filter, download and watch replays on the configured self-hosted Replay Kraken server."));
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
  const char *psz = r.bPending ? "..." : !r.bOk ? "?" : r.info.bHx ? "HX" : "CL";
  const UiColor c = r.bPending ? kLbInkDesc : !r.bOk ? kNoInk : r.info.bHx ? kHxInk : k117Ink;
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

int CurrentView(void)
{
  if (!g_activeCollection.empty()) {
    for (size_t i = 0; i < g_library.collections.size(); ++i)
      if (g_library.collections[i].id == g_activeCollection)
        return static_cast<int>(4 + g_library.queries.size() + i);
  }
  if (!g_activeQuery.empty()) {
    for (size_t i = 0; i < g_library.queries.size(); ++i)
      if (g_library.queries[i].id == g_activeQuery)
        return static_cast<int>(4 + i);
  }
  if (g_bFavouriteOnly)
    return 1;
  if (g_bStartedOnly)
    return 2;
  if (g_bNotesOnly)
    return 3;
  return 0;
}

void ApplyView(int choice)
{
  g_bFavouriteOnly = g_bStartedOnly = g_bNotesOnly = false;
  g_activeCollection.clear();
  g_activeQuery.clear();
  g_savedServerApi.clear();
  if (choice == 1)
    g_bFavouriteOnly = true;
  else if (choice == 2)
    g_bStartedOnly = true;
  else if (choice == 3)
    g_bNotesOnly = true;
  else if (choice >= 4 && choice < static_cast<int>(4 + g_library.queries.size())) {
    g_activeQuery = g_library.queries[choice - 4].id;
    const ReplayLibrary::Filters &f = g_library.queries[choice - 4].filters;
    g_search = f.search;
    g_onlineSearch = f.search;
    g_savedServerApi = f.serverApi;
    g_nFormat = f.format;
    g_nGame = f.game;
    g_bFavouriteOnly = f.bFavourites;
    g_bStartedOnly = f.bStarted;
    g_bNotesOnly = f.bNotes;
    g_query.player = f.who;
    g_query.matchup = f.matchup;
    g_query.race = f.race;
    g_query.result = f.result;
    g_query.from = f.from;
    g_query.to = f.to;
    g_query.followedBy = f.followedBy;
    g_nWho = 0;
    if (!f.who.empty()) {
      const auto who = std::find(g_who.begin(), g_who.end(), f.who);
      g_nWho = who == g_who.end() ? 0 : static_cast<int>(who - g_who.begin()) + 1;
    }
  } else if (choice >= static_cast<int>(4 + g_library.queries.size())) {
    const size_t index = static_cast<size_t>(choice - 4 - g_library.queries.size());
    if (index < g_library.collections.size())
      g_activeCollection = g_library.collections[index].id;
  }
  Filter();
  g_nFirst = 0;
}

ReplayLibrary::Filters CurrentFilters(void)
{
  ReplayLibrary::Filters filters;
  filters.search = g_search;
  filters.who = g_nWho > 0 && g_nWho <= static_cast<int>(g_who.size()) ? g_who[g_nWho - 1] : g_query.player;
  filters.matchup = g_query.matchup;
  filters.race = g_query.race;
  filters.result = g_query.result;
  filters.from = g_query.from;
  filters.to = g_query.to;
  filters.followedBy = g_query.followedBy;
  filters.serverApi = g_online.serverApi;
  filters.format = g_nFormat;
  filters.game = g_nGame;
  filters.bFavourites = g_bFavouriteOnly;
  filters.bStarted = g_bStartedOnly;
  filters.bNotes = g_bNotesOnly;
  return filters;
}

void SaveQuery(const std::string &name)
{
  ReplayLibrary::SavedQuery query;
  query.id = ReplayKraken::UuidNew();
  query.name = name.empty() ? PortText("Saved query") : name;
  query.filters = CurrentFilters();
  query.updatedAt = ReplayLibrary::NowSeconds();
  g_library.queries.push_back(query);
  g_activeQuery = query.id;
  g_activeCollection.clear();
  ReplayOnline::SaveLibrary(g_library);
}

void ToggleFixedMembership(void)
{
  const Replay *r = Picked();
  if (!r)
    return;
  auto list = std::find_if(g_library.collections.begin(), g_library.collections.end(), [](const auto &candidate) {
    return candidate.id == g_activeCollection;
  });
  if (list == g_library.collections.end())
    return;
  const auto at = std::find(list->recordIds.begin(), list->recordIds.end(), r->contentId);
  if (at == list->recordIds.end())
    list->recordIds.push_back(r->contentId);
  else
    list->recordIds.erase(at);
  list->updatedAt = ReplayLibrary::NowSeconds();
  ReplayOnline::SaveLibrary(g_library);
  Filter();
}

const char *UploadPhaseName(ReplayOnline::UploadPhase phase);

const ReplayOnline::UploadView *LatestOutboxJob(void)
{
  for (auto at = g_online.uploads.rbegin(); at != g_online.uploads.rend(); ++at)
    if (at->phase != ReplayOnline::UploadPhase::Uploaded)
      return &*at;
  return nullptr;
}

const ReplayOnline::UploadView *LatestRetryJob(void)
{
  for (auto at = g_online.uploads.rbegin(); at != g_online.uploads.rend(); ++at)
    if (at->bCommitted && at->phase != ReplayOnline::UploadPhase::Uploaded && at->phase != ReplayOnline::UploadPhase::Uploading &&
        at->phase != ReplayOnline::UploadPhase::Preparing && at->phase != ReplayOnline::UploadPhase::Queued &&
        at->phase != ReplayOnline::UploadPhase::RetryWaiting)
      return &*at;
  return nullptr;
}

void LibraryTools(void)
{
  Decl row;
  row.d.layout.sizing.width = Grow();
  row.d.layout.padding = Pad(Fs(12), Fs(12), Fs(4), Fs(8));
  row.d.layout.childGap = Gap(Fs(8));
  row.d.layout.childAlignment.y = CLAY_ALIGN_Y_CENTER;
  Open(row);
  std::vector<std::string> views = {PortText("All replays"), PortText("Favourites"), PortText("Started"), PortText("With notes")};
  for (const auto &query : g_library.queries)
    views.push_back(PortTextF("Query: {0}", {query.name}));
  for (const auto &list : g_library.collections)
    views.push_back(PortTextF("List: {0}", {list.name}));
  const int view = Pulldown("rpviews", 0, 220, views, CurrentView());
  if (view >= 0)
    ApplyView(view);
  Decl save = Box("rpsaveview");
  save.d.layout.sizing.height = Fixed(Sc(32));
  if (ScMini(save, PortText("Save query"), 0)) {
    g_bViewDialog = true;
    g_editName = PortText("Replay query");
  }
  Decl list = Box("rpnewlist");
  list.d.layout.sizing.height = Fixed(Sc(32));
  if (ScMini(list, PortText("New list"), 0, Picked() != nullptr)) {
    ReplayLibrary::Collection collection;
    collection.id = ReplayKraken::UuidNew();
    collection.name = PortTextF("Replay list {0}", {std::to_string(g_library.collections.size() + 1)});
    collection.recordIds = {Picked()->contentId};
    collection.updatedAt = ReplayLibrary::NowSeconds();
    g_library.collections.push_back(collection);
    g_activeCollection = collection.id;
    ReplayOnline::SaveLibrary(g_library);
    Filter();
  }
  if (!g_activeCollection.empty()) {
    Decl member = Box("rplistmember");
    member.d.layout.sizing.height = Fixed(Sc(32));
    if (ScMini(member, PortText("List +/-"), 0, Picked() != nullptr))
      ToggleFixedMembership();
  }
  Decl refresh = Box("rprefresh");
  refresh.d.layout.sizing.height = Fixed(Sc(32));
  if (ScMini(refresh, PortText("Refresh"), 'R'))
    ReplayCatalog::Refresh();
  if (const ReplayOnline::UploadView *pending = LatestOutboxJob()) {
    Text(Keep(PortTextF("Outbox: {0}", {PortText(UploadPhaseName(pending->phase))})), UI_FACE_TEXT, Fs(13), 0.0f,
         pending->phase == ReplayOnline::UploadPhase::Failed || pending->phase == ReplayOnline::UploadPhase::Conflict ? kNoInk : kLbInkDesc);
    if (pending->bCommitted && pending->phase != ReplayOnline::UploadPhase::Preparing &&
        pending->phase != ReplayOnline::UploadPhase::Uploading && pending->phase != ReplayOnline::UploadPhase::Queued &&
        pending->phase != ReplayOnline::UploadPhase::RetryWaiting) {
      Decl retry = Box("rpoutboxretry");
      retry.d.layout.sizing.height = Fixed(Sc(32));
      if (ScMini(retry, PortText("Retry outbox"), 0))
        ReplayOnline::Retry(pending->uuid);
    }
    if (pending->bCommitted) {
      Decl discard = Box("rpoutboxdiscard");
      discard.d.layout.sizing.height = Fixed(Sc(32));
      if (ScMini(discard, PortText("Discard job"), 0))
        ReplayOnline::Discard(pending->uuid);
    }
  }
  Decl import = Box("rpimport");
  import.d.layout.sizing.height = Fixed(Sc(32));
  if (ScMini(import, PortText("Import"), 0)) {
    g_bImportDialog = true;
    g_editPath = "replay-library-import.json";
  }
  Decl exportButton = Box("rpexport");
  exportButton.d.layout.sizing.height = Fixed(Sc(32));
  if (ScMini(exportButton, PortText("Export"), 0, Picked() != nullptr)) {
    g_bExportDialog = true;
    g_editPath = "replay-library-export.json";
    g_exportFavourites = g_exportNotes = g_exportQueries = g_exportCollections = false;
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
  LibraryTools();

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
    const int n = static_cast<int>((data.boundingBox.height - Sc(36) - Sc(108)) / fRow);
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

const ReplayOnline::UploadView *UploadForContent(const std::string &contentId)
{
  for (auto at = g_online.uploads.rbegin(); at != g_online.uploads.rend(); ++at)
    if (at->contentId == contentId)
      return &*at;
  return nullptr;
}

bool CanSubmit(const Replay &r)
{
  if (r.bPending || !r.bOk || IsDownloaded(r) || g_online.serverApi.empty())
    return false;
  const ReplayOnline::UploadView *job = UploadForContent(r.contentId);
  return !job || job->phase == ReplayOnline::UploadPhase::Failed || job->phase == ReplayOnline::UploadPhase::Conflict ||
         job->phase == ReplayOnline::UploadPhase::Unsupported;
}

void Submit(void)
{
  const Replay *r = Picked();
  if (!r || !CanSubmit(*r))
    return;
  ReplayKraken::UploadMetadata metadata;
  if (!ReplayCatalog::ReadUploadMetadata(*r, &metadata)) {
    g_note = PortText("The replay has no authoritative upload metadata.");
    return;
  }
  if (ReplayOnline::Submit(r->path, metadata, r->contentId, false).empty())
    g_note = PortText("The durable upload queue could not be started.");
  else
    g_note = PortText("Preparing an immutable upload snapshot; Queued appears only after it is committed.");
}

void ToggleFavourite(void)
{
  const Replay *r = Picked();
  if (!r || r->contentId.empty())
    return;
  ReplayLibrary::Record &record = ReplayLibrary::For(&g_library, r->contentId);
  record.bFavourite = !record.bFavourite;
  ReplayOnline::SaveLibrary(g_library);
}

void OpenNoteFor(const std::string &id)
{
  if (id.empty())
    return;
  g_noteTarget = id;
  g_bNoteDialog = true;
  g_bNoteTagsEdit = false;
  g_editNote.clear();
  g_editTags.clear();
  g_editNoteId.clear();
  g_editShared = false;
  if (const ReplayLibrary::Record *record = LibraryRecord(id); record && !record->notes.empty()) {
    const ReplayLibrary::Note &note = record->notes.front();
    g_editNoteId = note.id;
    g_editNote = note.text;
    g_editShared = note.bShared;
    for (size_t i = 0; i < note.tags.size(); ++i)
      g_editTags += (i ? ", " : "") + note.tags[i];
  }
}

void OpenNote(void)
{
  const Replay *r = Picked();
  if (r)
    OpenNoteFor(r->contentId);
}

const char *UploadPhaseName(ReplayOnline::UploadPhase phase)
{
  switch (phase) {
    case ReplayOnline::UploadPhase::Preparing: return "Preparing snapshot";
    case ReplayOnline::UploadPhase::Disabled: return "Disabled";
    case ReplayOnline::UploadPhase::Queued: return "Queued";
    case ReplayOnline::UploadPhase::Uploading: return "Uploading";
    case ReplayOnline::UploadPhase::RetryWaiting: return "Retry waiting";
    case ReplayOnline::UploadPhase::Uploaded: return "Uploaded";
    case ReplayOnline::UploadPhase::Failed: return "Failed";
    case ReplayOnline::UploadPhase::Conflict: return "Conflict / Needs review";
    case ReplayOnline::UploadPhase::Unsupported: return "Unsupported / Protocol";
  }
  return "Unknown";
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
  const bool bWait = v.nKind == VERDICT_WAIT;
  vb.d.backgroundColor = Col(bOkV ? kOkFill : bWait ? kScMenuFill : kNoFill);
  vb.d.border = Border(bOkV ? kOkInk : bWait ? kLbDivider : kNoInk, Fs(4), 0, 0, 0);
  Radius(vb, 3);
  Decl gap;
  gap.d.layout.sizing.height = Fixed(Sc(10));
  Open(gap);
  Close();
  Open(vb);
  Text(Keep(v.head), UI_FACE_BOLD, Fs(17), 0.0f, bOkV ? kOkInk : bWait ? kLbInkDesc : kNoInk);
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
    Fact(PortText("File"), r->rel + kDot + FileDate(FileTimeOf(r->when)));
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
  if (const ReplayOnline::UploadView *job = UploadForContent(r->contentId)) {
    Text(Keep(PortTextF("Upload: {0} · {1}", {PortText(UploadPhaseName(job->phase)), job->account.empty() ? PortText("account unknown") : job->account})),
         UI_FACE_TEXT, Fs(14), 0.0f, job->phase == ReplayOnline::UploadPhase::Uploaded ? kOkInk : kLbInkDesc, -1, 1.25f);
    if (!job->error.empty())
      Text(Keep(job->error), UI_FACE_TEXT, Fs(13), 0.0f, kNoInk, -1, 1.25f);
    if (job->bCommitted && (job->phase == ReplayOnline::UploadPhase::Failed || job->phase == ReplayOnline::UploadPhase::Conflict ||
        job->phase == ReplayOnline::UploadPhase::Unsupported || job->phase == ReplayOnline::UploadPhase::Disabled)) {
      Decl retry = Box("rpretry");
      retry.d.layout.sizing.height = Fixed(Sc(32));
      if (ScMini(retry, PortText("Retry upload"), 0))
        ReplayOnline::Retry(job->uuid);
    }
  }
  Decl row;
  row.d.layout.sizing.width = Grow();
  row.d.layout.childGap = Gap(Fs(4));
  Open(row);
  Decl sub2 = Box("rpsubmit");
  sub2.d.layout.sizing.height = Fixed(Sc(32));
  if (ScMini(sub2, PortText("Submit"), 'U', CanSubmit(*r)))
    Submit();
  TipOn(Id("rpsubmit"), PortText("Submit to Replay Kraken"),
        IsDownloaded(*r) ? PortText("Downloaded replays are never submitted again.")
                         : PortText("Commits an immutable snapshot, then uploads this locally recorded game with a stable UUID. Notes and tags are never included."));
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
  Decl organization;
  organization.d.layout.sizing.width = Grow();
  organization.d.layout.childGap = Gap(Fs(6));
  Open(organization);
  const ReplayLibrary::Record *libraryRecord = LibraryRecord(r->contentId);
  Decl favourite = Box("rp favourite");
  favourite.d.layout.sizing.height = Fixed(Sc(32));
  if (ScMini(favourite, libraryRecord && libraryRecord->bFavourite ? PortText("Favourited") : PortText("Favourite"), 0))
    ToggleFavourite();
  Decl notes = Box("rpnotes");
  notes.d.layout.sizing.height = Fixed(Sc(32));
  if (ScMini(notes, ReplayLibrary::HasNotes(g_library, r->contentId) ? PortText("Edit note") : PortText("Add note"), 0))
    OpenNote();
  Close();
  Close();
  Close();
}

std::string OnlineId(const ReplayKraken::Match &match)
{
  ReplayKraken::Endpoint endpoint;
  std::string error;
  return ReplayKraken::EndpointParse(match.serverApi, &endpoint, &error) ?
             ReplayKraken::ServerRecordId(endpoint, match.uuid) : "";
}

const ReplayOnline::DownloadView *DownloadFor(const ReplayKraken::Match &match)
{
  for (const auto &view : g_online.downloads)
    if (view.serverApi == match.serverApi && view.uuid == match.uuid)
      return &view;
  return nullptr;
}

const ReplayLibrary::Record *OnlineRecord(const ReplayKraken::Match &match)
{
  if (const ReplayOnline::DownloadView *download = DownloadFor(match); download && !download->contentId.empty()) {
    if (const ReplayLibrary::Record *linked = LibraryRecord(download->contentId))
      return linked;
  }
  return LibraryRecord(OnlineId(match));
}

const ReplayKraken::Match *OnlinePicked(void)
{
  return g_nOnlinePick >= 0 && g_nOnlinePick < static_cast<int>(g_online.list.page.matches.size())
             ? &g_online.list.page.matches[g_nOnlinePick] : nullptr;
}

bool OnlinePasses(const ReplayKraken::Match &match)
{
  if (!g_savedServerApi.empty() && match.serverApi != g_savedServerApi)
    return false;
  const std::string id = OnlineId(match);
  const ReplayLibrary::Record *record = OnlineRecord(match);
  if (g_query.bFavourites && (!record || !record->bFavourite))
    return false;
  if (g_query.bStarted && (!record || !record->startedAt))
    return false;
  if (g_query.bNotes && !(record && std::any_of(record->notes.begin(), record->notes.end(), [](const auto &note) {
        return !note.text.empty() || !note.tags.empty();
      })))
    return false;
  if (!g_activeCollection.empty()) {
    const auto list = std::find_if(g_library.collections.begin(), g_library.collections.end(), [](const auto &candidate) {
      return candidate.id == g_activeCollection;
    });
    const ReplayOnline::DownloadView *download = DownloadFor(match);
    const bool bMember = list != g_library.collections.end() &&
                         (ReplayLibrary::InCollection(*list, id) ||
                          (download && ReplayLibrary::InCollection(*list, download->contentId)));
    if (!bMember)
      return false;
  }
  if (!g_query.player.empty()) {
    const std::string needle = PortLower(g_query.player);
    const bool bFound = std::any_of(match.players.begin(), match.players.end(), [&](const auto &player) {
      return PortLower(player.name).find(needle) != std::string::npos;
    });
    if (!bFound)
      return false;
  }
  if (!g_query.race.empty()) {
    const std::string race = PortLower(g_query.race);
    const bool bFound = std::any_of(match.players.begin(), match.players.end(), [&](const auto &player) {
      return PortLower(player.race) == race;
    });
    if (!bFound)
      return false;
  }
  if (!g_query.result.empty()) {
    const std::string result = PortLower(g_query.result);
    const bool bFound = std::any_of(match.players.begin(), match.players.end(), [&](const auto &player) {
      return PortLower(player.result) == result;
    });
    if (!bFound)
      return false;
  }
  if (!g_query.matchup.empty() && ReplayKraken::MatchupKey(match.matchup) != ReplayKraken::MatchupKey(g_query.matchup))
    return false;
  if (!g_onlineSearch.empty()) {
    std::string hay = PortLower(match.map + " " + match.gameVersion + " " + match.uploadedBy + " " + match.uuid + " " +
                                match.gameType + " " + match.matchup);
    for (const auto &player : match.players)
      hay += " " + PortLower(player.name);
    if (hay.find(PortLower(g_onlineSearch)) == std::string::npos)
      return false;
  }
  return true;
}

void OnlineFilter(void)
{
  const std::string uuid = OnlinePicked() ? OnlinePicked()->uuid : "";
  g_onlineShown.clear();
  g_nOnlinePick = -1;
  for (size_t i = 0; i < g_online.list.page.matches.size(); ++i) {
    if (!OnlinePasses(g_online.list.page.matches[i]))
      continue;
    g_onlineShown.push_back(static_cast<int>(i));
    if (g_online.list.page.matches[i].uuid == uuid)
      g_nOnlinePick = static_cast<int>(i);
  }
  if (g_nOnlinePick < 0 && !g_onlineShown.empty())
    g_nOnlinePick = g_onlineShown.front();
}

void OnlineMove(int by)
{
  if (g_onlineShown.empty())
    return;
  int at = 0;
  for (size_t i = 0; i < g_onlineShown.size(); ++i)
    if (g_onlineShown[i] == g_nOnlinePick)
      at = static_cast<int>(i);
  at = std::max(0, std::min(at + by, static_cast<int>(g_onlineShown.size()) - 1));
  g_nOnlinePick = g_onlineShown[at];
}

const Replay *OnlineLocalReplay(const ReplayKraken::Match &match)
{
  const ReplayOnline::DownloadView *download = DownloadFor(match);
  if (!download || download->phase != ReplayOnline::DownloadPhase::Ready)
    return nullptr;
  for (const Replay &entry : g_reps)
    if (entry.path == download->path)
      return &entry;
  const auto info = g_downloadInfo.find(match.serverApi + ":" + match.uuid);
  if (info == g_downloadInfo.end())
    return nullptr;
  static Replay local;
  local = Replay();
  local.path = download->path;
  local.rel = download->path;
  local.contentId = download->contentId;
  local.name = match.map.empty() ? match.uuid : match.map;
  local.bPending = false;
  local.bOk = true;
  local.info = info->second;
  local.bExpansion = local.info.abHead[0] != 0;
  return &local;
}

void OnlineWatch(void)
{
  const ReplayKraken::Match *match = OnlinePicked();
  if (!match)
    return;
  const Replay *local = OnlineLocalReplay(*match);
  if (!local || !CanWatch(*local))
    return;
  const ReplayOnline::DownloadView *download = DownloadFor(*match);
  WatchPath(local->path, download->contentId, match->uuid);
}

void OnlineTools(void)
{
  Decl tools;
  tools.d.layout.sizing.width = Grow();
  tools.d.layout.padding = Pad(Fs(12), Fs(12), Fs(10), Fs(10));
  tools.d.layout.childGap = Gap(Fs(8));
  tools.d.layout.childAlignment.y = CLAY_ALIGN_Y_CENTER;
  tools.d.border = Border(kLbDivider, 0, 0, 0, Fs(2));
  Open(tools);
  Decl search = Box("rponlinesearch");
  search.d.layout.sizing.width = Fixed(Sc(280));
  search.d.layout.sizing.height = Fixed(Sc(36));
  if (Clicked(search.d.id))
    g_bOnlineSearch = true;
  else if (g_bPressed && !Over(search.d.id))
    g_bOnlineSearch = false;
  const std::string was = g_onlineSearch;
  ScEdit(search, &g_onlineSearch, 120, PortText("Search fetched rows"), g_bOnlineSearch);
  if (was != g_onlineSearch) {
    g_query.search = g_onlineSearch;
    OnlineFilter();
  }
  Decl player = Box("rponlineplayer");
  player.d.layout.sizing.width = Fixed(Sc(190));
  player.d.layout.sizing.height = Fixed(Sc(36));
  if (Clicked(player.d.id)) {
    g_bOnlinePlayerEdit = true;
    g_bOnlineSearch = g_bOnlineMatchupEdit = false;
  }
  ScEdit(player, &g_query.player, 80, PortText("Player (archive)"), g_bOnlinePlayerEdit);
  Decl matchup = Box("rponlinematchup");
  matchup.d.layout.sizing.width = Fixed(Sc(125));
  matchup.d.layout.sizing.height = Fixed(Sc(36));
  if (Clicked(matchup.d.id)) {
    g_bOnlineMatchupEdit = true;
    g_bOnlineSearch = g_bOnlinePlayerEdit = false;
  }
  ScEdit(matchup, &g_query.matchup, 24, PortText("TvZ"), g_bOnlineMatchupEdit);
  const std::vector<std::string> races = {PortText("Any race"), PortText("Zerg"), PortText("Terran"), PortText("Protoss")};
  int race = 0;
  if (!g_query.race.empty()) {
    const std::vector<std::string> keys = {"", "zerg", "terran", "protoss"};
    const auto at = std::find(keys.begin(), keys.end(), PortLower(g_query.race));
    race = at == keys.end() ? 0 : static_cast<int>(at - keys.begin());
  }
  const int chosenRace = Pulldown("rponlinerace", 0, 145, races, race);
  if (chosenRace >= 0) {
    static const char *const keys[] = {"", "zerg", "terran", "protoss"};
    g_query.race = keys[chosenRace];
  }
  Decl apply = Box("rp online apply");
  apply.d.layout.sizing.height = Fixed(Sc(32));
  if (ScMini(apply, PortText("Apply"), 0, !g_online.serverApi.empty()))
    ReplayOnline::Fetch(g_query, true);
  Decl following = Box("rponlinefollowing");
  following.d.layout.sizing.height = Fixed(Sc(32));
  if (ScMini(following, g_query.followedBy.empty() ? PortText("Following only") : PortText("All matches"), 0,
             g_online.bTokenPresent)) {
    g_query.followedBy = g_query.followedBy.empty() ? "@me" : "";
    ReplayOnline::Fetch(g_query, true);
  }
  Decl more = Box("rp online more");
  more.d.layout.sizing.height = Fixed(Sc(32));
  const bool bMore = !g_online.list.bLoading && !g_online.list.page.bComplete && !g_online.list.bOffline;
  if (ScMini(more, g_online.list.bPaginating ? PortText("Loading…") : PortText("Load more"), 0, bMore))
    ReplayOnline::LoadMore();
  Close();
}

void OnlineList(void)
{
  Decl box = Box("rplist");
  box.d.layout.layoutDirection = CLAY_TOP_TO_BOTTOM;
  box.d.layout.sizing.width = Grow();
  box.d.layout.sizing.height = Grow();
  box.d.clip.vertical = true;
  InnerBox(box);
  Open(box);
  OnlineTools();
  Decl head;
  head.d.layout.sizing.height = Fixed(Sc(34));
  head.d.layout.padding = Pad(Fs(12), Fs(12), 0, 0);
  head.d.layout.childGap = Gap(Fs(12));
  Open(head);
  const char *const titles[] = {PortText("MAP / MATCH"), PortText("PLAYERS"), PortText("LENGTH"), PortText("UPLOADER"), PortText("VERSION"), PortText("STATE")};
  const float widths[] = {280, 130, 90, 145, 135, 120};
  for (int i = 0; i < 6; ++i) {
    Decl cell;
    cell.d.layout.sizing.width = Fixed(Sc(widths[i]));
    Open(cell);
    Text(titles[i], UI_FACE_WIDE, Fs(11), 0.03f, kScInkHead);
    Close();
  }
  Close();
  const int rows = g_nShownRows;
  const int maxFirst = std::max(0, static_cast<int>(g_onlineShown.size()) - rows);
  g_nOnlineFirst = std::max(0, std::min(g_nOnlineFirst, maxFirst));
  g_nOnlineFirst += WheelRows(Over(Id("rplist")));
  g_nOnlineFirst = std::max(0, std::min(g_nOnlineFirst, maxFirst));
  for (int k = g_nOnlineFirst; k < static_cast<int>(g_onlineShown.size()) && k < g_nOnlineFirst + rows; ++k) {
    const int index = g_onlineShown[k];
    const ReplayKraken::Match &match = g_online.list.page.matches[index];
    Decl row = Box("rponlinerow", index);
    const bool bPick = index == g_nOnlinePick;
    const bool bHot = Over(row.d.id);
    if (Clicked(row.d.id)) {
      g_nOnlinePick = index;
      if (DoubleClicked(row.d.id)) {
        if (DownloadFor(match) && DownloadFor(match)->phase == ReplayOnline::DownloadPhase::Ready)
          OnlineWatch();
        else
          ReplayOnline::Download(match);
      }
    }
    row.d.layout.sizing.width = Grow();
    row.d.layout.sizing.height = Fixed(Sc(58));
    row.d.layout.padding = Pad(Fs(12), Fs(12), 0, 0);
    row.d.layout.childGap = Gap(Fs(12));
    row.d.layout.childAlignment.y = CLAY_ALIGN_Y_CENTER;
    row.d.border = Border(bPick ? kScSelLine2 : kLbDivider, bPick ? Fs(3) : 0, 0, 0, 1);
    if (bPick)
      row.d.backgroundColor = Col(kLbItemHi);
    else if (bHot)
      row.d.backgroundColor = Col(kLbItem);
    Open(row);
    const float widths[] = {280, 130, 90, 145, 135, 120};
    const std::string duration = Clock(match.durationFrames, 6);
    const std::string players = match.matchup.empty() ? PortText("Unknown") : match.matchup;
    const ReplayOnline::DownloadView *download = DownloadFor(match);
    std::string state = PortText("Not downloaded");
    if (download) {
      switch (download->phase) {
        case ReplayOnline::DownloadPhase::Queued: state = PortText("Queued"); break;
        case ReplayOnline::DownloadPhase::Downloading: state = PortText("Downloading"); break;
        case ReplayOnline::DownloadPhase::Validating: state = PortText("Validating"); break;
        case ReplayOnline::DownloadPhase::Ready: state = PortText("Downloaded"); break;
        case ReplayOnline::DownloadPhase::Failed: state = PortText("Download failed"); break;
      }
    }
    const std::string values[] = {match.map.empty() ? PortText("Unknown map") : match.map, players, duration,
                                  match.uploadedBy.empty() ? PortText("Unknown") : match.uploadedBy,
                                  match.gameVersion.empty() ? PortText("Unknown") : match.gameVersion, state};
    for (int i = 0; i < 6; ++i) {
      Decl cell;
      cell.d.layout.sizing.width = Fixed(Sc(widths[i]));
      cell.d.clip.horizontal = true;
      Open(cell);
      Text(Keep(Elide(values[i], UI_FACE_TEXT, Fs(i == 0 ? 16 : 14), Sc(widths[i]))), UI_FACE_TEXT,
           Fs(i == 0 ? 16 : 14), 0.0f, bPick || bHot ? kScInkHi : kLbInkInfo);
      Close();
    }
    Close();
  }
  if (g_onlineShown.empty()) {
    Decl none;
    none.d.layout.padding = Pad(Fs(24), Fs(24), Fs(24), Fs(24));
    Open(none);
    Text(Keep(g_online.list.bLoading ? std::string(PortText("Loading the Replay Kraken archive…")) :
         !g_online.list.error.empty() ? g_online.list.error :
         g_online.list.page.matches.empty() ? std::string(PortText("No online matches. Configure the server in Settings, or refresh.")) :
                                             std::string(PortText("No fetched row matches these filters."))),
         UI_FACE_TEXT, Fs(16), 0.0f, g_online.list.bUnauthorized ? kNoInk : kLbInkDesc, -1, 1.3f);
    Close();
  }
  if (!g_online.list.error.empty() && !g_online.list.page.matches.empty())
    Text(Keep((g_online.list.bStale ? PortText("Offline · showing stale cache · ") : PortText("Offline · ")) + g_online.list.error),
         UI_FACE_TEXT, Fs(14), 0.0f, kNoInk, -1, 1.25f);
  if (!g_online.lastError.empty())
    Text(Keep(g_online.lastError), UI_FACE_TEXT, Fs(13), 0.0f, kNoInk, -1, 1.25f);
  Close();
}

void OnlineSide(void)
{
  Decl side = Box("rpside");
  side.d.layout.layoutDirection = CLAY_TOP_TO_BOTTOM;
  side.d.layout.sizing.width = Fixed(Sc(kSide));
  side.d.layout.sizing.height = Grow();
  InnerBox(side);
  Open(side);
  Decl body = Box("rpsidebody");
  body.d.layout.sizing.width = Grow();
  body.d.layout.sizing.height = Grow();
  body.d.clip.vertical = true;
  body.d.layout.padding = Pad(Fs(18), Fs(18), Fs(16), Fs(12));
  body.d.layout.layoutDirection = CLAY_TOP_TO_BOTTOM;
  body.d.layout.childGap = Gap(Fs(8));
  Open(body);
  const ReplayKraken::Match *match = OnlinePicked();
  if (!match) {
    Text(PortText("Pick an online replay."), UI_FACE_TEXT, Fs(16), 0.0f, kLbInkDesc);
    Close();
    Close();
    return;
  }
  Text(Keep(match->map.empty() ? PortTextF("Replay {0}", {match->uuid.substr(0, 8)}) : match->map), UI_FACE_BOLD,
       Fs(23), 0.0f, kLbInkInfo);
  Text(Keep((match->matchup.empty() ? PortText("Matchup unknown") : match->matchup) + kDot + match->uploadedAt),
       UI_FACE_TEXT, Fs(14), 0.0f, kLbInkDesc);
  Fact(PortText("Game version"), match->gameVersion.empty() ? PortText("Unknown") : match->gameVersion);
  Fact(PortText("Game type"), match->gameType.empty() ? PortText("Unknown") : match->gameType);
  Fact(PortText("Duration"), match->durationFrames ? Clock(match->durationFrames, 6) + kDot + Thousands(match->durationFrames) + " frames" : PortText("Unknown"));
  Fact(PortText("Uploader"), match->uploadedBy.empty() ? PortText("Unknown") : match->uploadedBy);
  Fact(PortText("Server"), match->serverOrigin);
  for (const auto &player : match->players) {
    Decl row;
    row.d.layout.childGap = Gap(Fs(8));
    row.d.layout.childAlignment.y = CLAY_ALIGN_Y_CENTER;
    Open(row);
    Chip(0xb4b4b4, 12);
    Text(Keep(Elide(player.name.empty() ? PortText("Unknown") : player.name, UI_FACE_BOLD, Fs(14), Sc(180))),
         UI_FACE_BOLD, Fs(14), 0.0f, kScInkHi);
    Text(Keep(player.race.empty() ? PortText("Unknown") : player.race), UI_FACE_TEXT, Fs(14), 0.0f, kLbInkDesc);
    Text(Keep(PortTextF("slot {0} · {1} · {2}", {std::to_string(player.slot),
          player.type.empty() ? PortText("type unknown") : player.type,
          player.result.empty() ? PortText("result unknown") : player.result})), UI_FACE_TEXT, Fs(13), 0.0f, kLbInkDesc);
    Close();
  }
  if (match->players.empty())
    Text(PortText("Players and results: Unknown."), UI_FACE_TEXT, Fs(14), 0.0f, kLbInkDesc);
  if (!match->players.empty()) {
    const ReplayKraken::Player *target = &match->players.front();
    for (const auto &player : match->players)
      if (player.type == "human") {
        target = &player;
        break;
      }
    const bool bFollowing = std::find(g_online.following.begin(), g_online.following.end(), target->name) != g_online.following.end();
    Decl follow = Box("rpfollowplayer");
    follow.d.layout.sizing.height = Fixed(Sc(32));
    const std::string label = PortTextF("{0} {1}", {bFollowing ? PortText("Unfollow") : PortText("Follow"),
                                      Elide(target->name, UI_FACE_TEXT, Fs(14), Sc(220))});
    if (ScMini(follow, Keep(label), 0, g_online.bTokenPresent))
      ReplayOnline::Follow(target->name, !bFollowing);
    TipOn(Id("rpfollowplayer"), PortText("Replay Kraken follow"),
          PortText("Uses the configured server's real follow endpoint. Following-only filtering then applies archive-wide."));
  }
  const Replay *local = OnlineLocalReplay(*match);
  if (local) {
    const Verdict verdict = VerdictOf(*local);
    Text(Keep(verdict.head), UI_FACE_BOLD, Fs(16), 0.0f, CanWatch(*local) ? kOkInk : kNoInk);
    Text(Keep(verdict.line), UI_FACE_TEXT, Fs(14), 0.0f, kLbInkInfo, -1, 1.3f);
  }
  const ReplayOnline::UploadView *job = nullptr;
  for (const auto &candidate : g_online.uploads)
    if (candidate.uuid == match->uuid && candidate.serverApi == match->serverApi && !candidate.account.empty() &&
        candidate.account == match->uploadedBy)
      job = &candidate;
  Fact(PortText("Upload state"), job ? PortText(UploadPhaseName(job->phase)) : PortText("Unknown"));
  const ReplayLibrary::Record *record = OnlineRecord(*match);
  Decl organization;
  organization.d.layout.childGap = Gap(Fs(8));
  Open(organization);
  const std::string id = record ? record->id : OnlineId(*match);
  Decl favourite = Box("rp online favourite");
  favourite.d.layout.sizing.height = Fixed(Sc(32));
  if (ScMini(favourite, record && record->bFavourite ? PortText("Favourited") : PortText("Favourite"), 0)) {
    ReplayLibrary::Record &edit = ReplayLibrary::For(&g_library, id);
    edit.bFavourite = !edit.bFavourite;
    ReplayOnline::SaveLibrary(g_library);
  }
  Decl note = Box("rp online note");
  note.d.layout.sizing.height = Fixed(Sc(32));
  if (ScMini(note, PortText("Note / tags"), 0))
    OpenNoteFor(id);
  Close();
  const ReplayOnline::DownloadView *download = DownloadFor(*match);
  Decl actions;
  actions.d.layout.childGap = Gap(Fs(8));
  Open(actions);
  if (!download || download->phase == ReplayOnline::DownloadPhase::Failed) {
    Decl get = Box("rpdownload");
    get.d.layout.sizing.height = Fixed(Sc(44));
    if (ScButton(get, PortText("Download"), 'D', true, g_online.bAvailable))
      ReplayOnline::Download(*match);
  } else if (download->phase == ReplayOnline::DownloadPhase::Ready) {
    Decl watch = Box("rp online watch");
    watch.d.layout.sizing.height = Fixed(Sc(44));
    if (ScButton(watch, PortText("Watch Replay"), 'W', true, local && CanWatch(*local)))
      OnlineWatch();
    if (!local || !CanWatch(*local))
      TipOn(Id("rp online watch"), PortText("Cannot watch"), PortText("The download is being indexed, or Hyperion refused its rules, data, simulation or mod requirements."));
  } else {
    Text(PortText("Download in progress; Hyperion validation follows before install."), UI_FACE_TEXT, Fs(14), 0.0f, kLbInkDesc);
  }
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

std::vector<std::string> NoteTags(const std::string &text)
{
  std::vector<std::string> tags;
  size_t at = 0;
  while (at <= text.size() && tags.size() < 32) {
    const size_t comma = text.find(',', at);
    std::string tag = text.substr(at, comma == std::string::npos ? std::string::npos : comma - at);
    const size_t first = tag.find_first_not_of(" \t\r\n");
    const size_t last = tag.find_last_not_of(" \t\r\n");
    if (first != std::string::npos) {
      tag = tag.substr(first, last - first + 1);
      if (tag.size() <= 128 && std::find(tags.begin(), tags.end(), tag) == tags.end())
        tags.push_back(tag);
    }
    if (comma == std::string::npos)
      break;
    at = comma + 1;
  }
  return tags;
}

void SaveNote(void)
{
  if (g_noteTarget.empty())
    return;
  ReplayLibrary::Record &record = ReplayLibrary::For(&g_library, g_noteTarget);
  if (g_editNoteId.empty())
    g_editNoteId = ReplayKraken::UuidNew();
  ReplayLibrary::Note note;
  note.id = g_editNoteId;
  note.text = g_editNote.substr(0, 65536);
  note.tags = NoteTags(g_editTags);
  note.bShared = g_editShared;
  note.updatedAt = ReplayLibrary::NowSeconds();
  bool bFound = false;
  for (ReplayLibrary::Note &old : record.notes)
    if (old.id == note.id) {
      old = note;
      bFound = true;
      break;
    }
  if (!bFound)
    record.notes.push_back(note);
  ReplayOnline::SaveLibrary(g_library);
  g_bNoteDialog = false;
}

void NoteDialog(void)
{
  const bool bOut = DialogOpen("rpnotedlg", 820, 430, 360);
  DialogTitle(PortText("REPLAY NOTE AND TAGS"));
  Decl text = Box("rpnotetext");
  text.d.layout.sizing.width = Grow();
  text.d.layout.sizing.height = Fixed(Sc(48));
  if (Clicked(text.d.id))
    g_bNoteTagsEdit = false;
  ScEdit(text, &g_editNote, 2000, PortText("Private note (one line)"), !g_bNoteTagsEdit);
  Decl tags = Box("rpnotetags");
  tags.d.layout.sizing.width = Grow();
  tags.d.layout.sizing.height = Fixed(Sc(44));
  if (Clicked(tags.d.id))
    g_bNoteTagsEdit = true;
  ScEdit(tags, &g_editTags, 512, PortText("Tags, comma separated"), g_bNoteTagsEdit);
  Decl visibility = Box("rpnotevisibility");
  visibility.d.layout.sizing.height = Fixed(Sc(34));
  if (ScMini(visibility, g_editShared ? PortText("Shared (export allowed)") : PortText("Private (never exported)"), 0))
    g_editShared = !g_editShared;
  Text(PortText("Private is the default. Uploading a replay never includes its note or tags."),
       UI_FACE_TEXT, Fs(14), 0.0f, kLbInkDesc);
  Decl row;
  row.d.layout.childGap = Gap(Fs(12));
  Open(row);
  Decl save = Box("rpnotesave");
  save.d.layout.sizing.width = Fixed(Sc(210));
  save.d.layout.sizing.height = Fixed(Sc(48));
  const bool bSave = ScButton(save, PortText("Save note"), 0, true);
  Decl cancel = Box("rpnotecancel");
  cancel.d.layout.sizing.width = Fixed(Sc(210));
  cancel.d.layout.sizing.height = Fixed(Sc(48));
  const bool bCancel = ScButton(cancel, PortText("Cancel"), 0, false);
  Close();
  Close();
  if (bSave)
    SaveNote();
  else if (bCancel || bOut)
    g_bNoteDialog = false;
}

void ViewDialog(void)
{
  const bool bOut = DialogOpen("rpviewdlg", 700, 240, 260);
  DialogTitle(PortText("SAVE DYNAMIC QUERY"));
  Decl edit = Box("rpviewname");
  edit.d.layout.sizing.width = Grow();
  edit.d.layout.sizing.height = Fixed(Sc(44));
  ScEdit(edit, &g_editName, 100, PortText("Query name"), true);
  Text(PortText("The complete filter is saved, not the currently rendered rows."), UI_FACE_TEXT, Fs(14), 0.0f, kLbInkDesc);
  Decl save = Box("rpviewsave");
  save.d.layout.sizing.height = Fixed(Sc(46));
  const bool bSave = ScButton(save, PortText("Save query"), 0, true);
  Close();
  if (bSave) {
    SaveQuery(g_editName);
    g_bViewDialog = false;
  } else if (bOut) {
    g_bViewDialog = false;
  }
}

void RunLibraryTransfer(bool bImport)
{
  if (bImport) {
    ReplayOnline::ImportLibrary(g_editPath);
    g_bImportDialog = false;
    return;
  }
  ReplayLibrary::ExportSelection selection;
  if (g_nTab == 0 && Picked())
    selection.recordIds.push_back(Picked()->contentId);
  else if (g_nTab == 1 && OnlinePicked()) {
    const ReplayLibrary::Record *record = OnlineRecord(*OnlinePicked());
    selection.recordIds.push_back(record ? record->id : OnlineId(*OnlinePicked()));
  }
  if (g_exportQueries)
    for (const auto &query : g_library.queries)
      selection.queryIds.push_back(query.id);
  if (g_exportCollections)
    for (const auto &list : g_library.collections)
      selection.collectionIds.push_back(list.id);
  selection.bFavourites = g_exportFavourites;
  selection.bSharedNotes = g_exportNotes;
  ReplayOnline::ExportLibrary(selection, g_editPath);
  g_bExportDialog = false;
}

void ExportImportDialog(bool bImport)
{
  const bool bOut = DialogOpen(bImport ? "rpimportdlg" : "rpexportdlg", 820, bImport ? 240 : 390, 340);
  DialogTitle(bImport ? PortText("IMPORT REPLAY LIBRARY") : PortText("EXPORT REPLAY LIBRARY"));
  Decl path = Box("rplibrarypath");
  path.d.layout.sizing.width = Grow();
  path.d.layout.sizing.height = Fixed(Sc(44));
  ScEdit(path, &g_editPath, 2048, PortText("JSON file path"), true);
  if (!bImport) {
    Decl fav = Box("rpexportfav");
    fav.d.layout.sizing.height = Fixed(Sc(32));
    if (ScMini(fav, g_exportFavourites ? PortText("[x] Include favourite") : PortText("[ ] Include favourite"), 0))
      g_exportFavourites = !g_exportFavourites;
    Decl notes = Box("rpexportnotes");
    notes.d.layout.sizing.height = Fixed(Sc(32));
    if (ScMini(notes, g_exportNotes ? PortText("[x] Include shared notes/tags") : PortText("[ ] Include shared notes/tags"), 0))
      g_exportNotes = !g_exportNotes;
    Decl queries = Box("rpexportqueries");
    queries.d.layout.sizing.height = Fixed(Sc(32));
    if (ScMini(queries, g_exportQueries ? PortText("[x] Include saved queries") : PortText("[ ] Include saved queries"), 0))
      g_exportQueries = !g_exportQueries;
    Decl lists = Box("rpexportlists");
    lists.d.layout.sizing.height = Fixed(Sc(32));
    if (ScMini(lists, g_exportCollections ? PortText("[x] Include fixed lists") : PortText("[ ] Include fixed lists"), 0))
      g_exportCollections = !g_exportCollections;
    Text(PortText("Only this replay and explicitly selected content is exported. Private notes and their tags are always excluded."),
         UI_FACE_TEXT, Fs(14), 0.0f, kLbInkDesc);
  }
  Decl run = Box("rplibraryrun");
  run.d.layout.sizing.height = Fixed(Sc(46));
  const bool bRun = ScButton(run, bImport ? PortText("Import and merge") : PortText("Export"), 0, true);
  Close();
  if (bRun) {
    RunLibraryTransfer(bImport);
  } else if (bOut) {
    g_bImportDialog = g_bExportDialog = false;
  }
}

void PumpServices(void)
{
  static uint64_t lastCatalogSync = 0;
  const bool catalogChanged = ReplayCatalog::Pump();
  const ReplayCatalog::Status catalogStatus = ReplayCatalog::GetStatus();
  const uint64_t now = PortNowMs();
  if (catalogChanged && ((catalogStatus.pending == 0 && !catalogStatus.bScanning) || now - lastCatalogSync >= 250)) {
    lastCatalogSync = now;
    SyncCatalog();
  }
  if (const ReplayKraken::Match *selected = OnlinePicked())
    g_onlineKeep = selected->uuid;
  const void *oldData = g_onlinePageData;
  const size_t oldSize = g_onlinePageSize;
  g_online = ReplayOnline::Snapshot();
  if (!g_onlineKeep.empty())
    for (size_t i = 0; i < g_online.list.page.matches.size(); ++i)
      if (g_online.list.page.matches[i].uuid == g_onlineKeep)
        g_nOnlinePick = static_cast<int>(i);
  if (oldData != g_online.list.page.matches.data() || oldSize != g_online.list.page.matches.size() || g_onlineShown.empty()) {
    OnlineFilter();
    g_onlinePageData = g_online.list.page.matches.data();
    g_onlinePageSize = g_online.list.page.matches.size();
  }
  for (const ReplayOnline::DownloadView &download : g_online.downloads) {
    if (download.phase == ReplayOnline::DownloadPhase::Failed)
      g_validationPosted.erase(download.serverApi + ":" + download.uuid);
    if (download.phase == ReplayOnline::DownloadPhase::Validating && !download.tempPath.empty() &&
        g_validationPosted.insert(download.serverApi + ":" + download.uuid).second) {
      ReplayHxInfo info;
      std::vector<unsigned char> chk;
      const bool bValid = ReplayHxPeek(download.tempPath.c_str(), &info, &chk);
      if (bValid)
        g_downloadInfo[download.serverApi + ":" + download.uuid] = info;
      ReplayOnline::DownloadValidated(download.serverApi, download.uuid, bValid,
                                      bValid ? "" : "The download is not a complete replay Hyperion can read.");
    }
    if (download.phase == ReplayOnline::DownloadPhase::Ready && !download.path.empty()) {
      const std::string key = download.serverApi + ":" + download.uuid;
      if (g_handledDownloads.insert(key).second) {
        g_selectAfterDownload = download.path;
        g_keep = download.path;
        g_keepContent = download.contentId;
        ReplayCatalog::Refresh();
      }
    }
  }
  g_library = ReplayOnline::LibrarySnapshot();
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
  g_bRename = g_bDelete = g_bSearch = g_bOnlineSearch = false;
  ReplayOnline::Start(PortDataFolder(), PortProgramFolder());
  char server[2048] = {};
  PortSettingString("hyperion", "replayKrakenServer", server, sizeof server);
  double automatic = 0.0;
  PortSettingNumber("hyperion", "replayKrakenAuto", &automatic);
  ReplayOnline::Configure(server, automatic != 0.0);
  g_library = ReplayOnline::LibrarySnapshot();
  ListReplays();
  ReplayOnline::Fetch(g_query, true);
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
  g_keep = Picked() != NULL ? Picked()->path : g_keep;
  g_keepContent = Picked() != NULL ? Picked()->contentId : g_keepContent;
  g_nPick = -1;
  g_shown.clear();
  g_pic = MapPicture();
  g_picContent.clear();
  const ReplayCatalog::Status status = ReplayCatalog::GetStatus();
  FrontLog("replays catalog: worst ReplayHxPeek %.3f ms, last engine batch %.3f ms\n", status.worstEnginePeekMs,
           status.lastEngineBatchMs);
  ReplayCatalog::Close();
}

int ReplaysResult(void)
{
  return g_nResult;
}

void ReplaysScreen(void)
{
  PumpServices();
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
  if (g_nTab == 0) {
    const ReplayCatalog::Status status = ReplayCatalog::GetStatus();
    std::string line = PortTextNF("{0} replay", "{0} replays", static_cast<long long>(g_reps.size()),
                                  {std::to_string(g_reps.size())});
    if (status.pending || status.bScanning)
      line += kDot + PortTextF("indexing {0}", {std::to_string(status.pending)});
    if (status.deferred)
      line += kDot + PortTextF("{0} still being written", {std::to_string(status.deferred)});
    if (status.bLimited)
      line += kDot + PortTextF("limit {0} (Settings)", {std::to_string(status.limit)});
    Head(PortText("My Replays"), line);
  } else {
    std::string line = g_online.serverApi.empty() ? PortText("Set the Replay Kraken server in Settings · Data and mods") : g_online.serverApi;
    line += kDot + PortTextF("{0} archive matches", {std::to_string(g_online.list.page.total)});
    if (g_online.list.bStale)
      line += std::string(kDot) + PortText("stale offline cache");
    Head(PortText("Online Replays"), line);
  }
  Decl body;
  body.d.layout.sizing.width = Grow();
  body.d.layout.sizing.height = Grow();
  body.d.layout.childGap = Gap(Fs(14));
  Open(body);
  if (g_nTab == 0) {
    List();
    Side();
  } else {
    OnlineList();
    OnlineSide();
  }
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
  if (g_bNoteDialog)
    NoteDialog();
  if (g_bViewDialog)
    ViewDialog();
  if (g_bExportDialog)
    ExportImportDialog(false);
  if (g_bImportDialog)
    ExportImportDialog(true);
  DrawTip();
  PulldownCloseOutside();
}

bool ReplaysKeys(void)
{
  if (g_bNoteDialog) {
    if (Key(VK_TAB))
      g_bNoteTagsEdit = !g_bNoteTagsEdit;
    else if (Key('V'))
      g_editShared = !g_editShared;
    else if (Key(VK_RETURN))
      SaveNote();
    else if (Key(VK_ESCAPE))
      g_bNoteDialog = false;
    return true;
  }
  if (g_bViewDialog) {
    if (Key(VK_RETURN)) {
      SaveQuery(g_editName);
      g_bViewDialog = false;
    } else if (Key(VK_ESCAPE))
      g_bViewDialog = false;
    return true;
  }
  if (g_bExportDialog || g_bImportDialog) {
    const bool bImport = g_bImportDialog;
    if (Key(VK_RETURN))
      RunLibraryTransfer(bImport);
    else if (Key(VK_ESCAPE))
      g_bExportDialog = g_bImportDialog = false;
    return true;
  }
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
  if (g_bSearch || g_bOnlineSearch || g_bOnlinePlayerEdit || g_bOnlineMatchupEdit) {
    if (Key(VK_ESCAPE) || Key(VK_RETURN))
      g_bSearch = g_bOnlineSearch = g_bOnlinePlayerEdit = g_bOnlineMatchupEdit = false;
    else if (Key(VK_DOWN))
      g_nTab == 0 ? Move(1) : OnlineMove(1);
    else if (Key(VK_UP))
      g_nTab == 0 ? Move(-1) : OnlineMove(-1);
    return true;
  }
  if (Key('1') || Key('2')) {
    g_nTab = Key('1') ? 0 : 1;
    if (g_nTab == 1)
      ReplayOnline::Fetch(g_query, true);
    else
      ReplayCatalog::Refresh();
    return true;
  }
  if (Key(VK_DOWN))
    g_nTab == 0 ? Move(1) : OnlineMove(1);
  else if (Key(VK_UP))
    g_nTab == 0 ? Move(-1) : OnlineMove(-1);
  else if (Key(VK_NEXT))
    g_nTab == 0 ? Move(g_nShownRows) : OnlineMove(g_nShownRows);
  else if (Key(VK_PRIOR))
    g_nTab == 0 ? Move(-g_nShownRows) : OnlineMove(-g_nShownRows);
  else if (Key('W') || Key(VK_RETURN))
    g_nTab == 0 ? Watch() : OnlineWatch();
  else if (Key('D') && g_nTab == 1 && OnlinePicked())
    ReplayOnline::Download(*OnlinePicked());
  else if (Key('M') && g_nTab == 1)
    ReplayOnline::LoadMore();
  else if (Key('U') && g_nTab == 0)
    Submit();
  else if (Key('V') && g_nTab == 0)
    ToggleFavourite();
  else if (Key('T') && g_nTab == 0 && Picked())
    OpenNote();
  else if (Key('R'))
    g_nTab == 0 ? ReplayCatalog::Refresh() : ReplayOnline::Fetch(g_query, true);
  else if (Key('O') && g_nTab == 0 && LatestRetryJob())
    ReplayOnline::Retry(LatestRetryJob()->uuid);
  else if (Key('N') && g_nTab == 0 && Picked() != NULL) {
    g_bRename = true;
    g_newName = Picked()->name;
    g_typed.clear();
  } else if (Key('F') && g_nTab == 0)
    ShowInFolder();
  else if (Key(VK_DELETE) && g_nTab == 0 && Picked() != NULL)
    g_bDelete = true;
  else if (Key(VK_ESCAPE) || Key('B'))
    g_nResult = FRONT_REPLAYS_HOME;
  return true;
}

}  // namespace FrontKit
