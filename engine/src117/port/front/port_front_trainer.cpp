// port_front_trainer.cpp -- the trainer's entry screen. Presentation only: Start
// Session hands a set-up to port_trainer.cpp
// (PortTrainerFromMenus), which makes the session's game, and the menus come back here, on the
// Sessions tab, when it ends.
//
// The trainer is a mod (mods\trainer\mod.json, mode "trainer") whose home screen button opens
// this screen; its header chooses where a session starts. A Skirmish is set up on the skirmish
// screen, hosting the trainer (SkirmishHost, port_frontscreens.h), with the Practice box in
// the mutators' place. A Replay picks one of the player's replays (maps\replays) and which of
// its people to take over as; the replay is the session's first line, watched, and an order
// of the player's takes over. Sessions lists those under Trainer\Sessions, newest first;
// opening one makes its game again and pauses at its start with the Tape up.
#include <windows.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <algorithm>
#include <set>
#include <string>
#include <vector>
#include <SDL3/SDL.h>
#include "port_frontkit.h"
#include "port_frontlobby.h"
#include "port_frontscreens.h"
#include "../gameplay/port_trainer.h"
#include "port_ui.h"
#include "Storm.h"        // SFileGetInstallPath
#include "glues.h"        // mem_free
#include "grid.h"         // IsBroodwar
#include "maphdr.h"       // get_map_chk
#include "../../h/port_hxreplay.h"  // ReplayHxPeek
#include "../data/port_stormapi.h"  // SFileGetUserDataPath
#include "../../gen/globals117.h"

using namespace FrontKit;

namespace {

const UiColor kTrGold = KitRgb(0xffe490);
const UiColor kInkWhite = KitRgb(0xffffff);
const UiColor kInkPlayer = KitRgb(0x64d0f9);
const UiColor kGoldRim = KitRgb(0xdaa308, 0.7f);
const UiColor kAttempt = KitRgb(0x9aa6b2, 0.7f);
const unsigned kLineColours[] = {0x66d5fe, 0xffe490, 0x7bd88f, 0xc792ea, 0xff8a65, 0x4fc3d9};

const int kPictureSlot = 15;  // the picture atlas's (the bench's set-up has 13, its run 14)

const char *const kRaceNames[] = {PT_("Zerg"), PT_("Terran"), PT_("Protoss"), PT_("Random")};
const char *const kRaceIds[] = {"zerg", "terran", "protoss", "random"};
const char kRaceLetters[] = "ztpr";
const char *const kSpeeds[] = {"Slowest", "Slower", "Slow", "Normal", "Fast", "Faster", "Fastest"};

struct Entry {
  int nTab = 0;       // New Session, Sessions
  int nSource = 0;    // a skirmish, a replay
  int nSpeed = 6;     // a replay's
  bool bEachMinute = false;
  bool bAsk = true;
  int nPick = 0;      // the session picked
} g_e;

// the player's replays: read as they are shown
struct Person {
  int nSlot;  // its player record, the game's slot
  std::string name;
  int nRace;  // 0 Zerg, 1 Terran, 2 Protoss
};
struct RepRow {
  std::string path, name;
  FILETIME when;
  bool bTried = false, bOk = false, bHx = false, bPlays = false;
  std::string map, why;
  unsigned dwFrames = 0;
  std::vector<Person> people;
  int nComputers = 0;
};
std::vector<RepRow> g_reps;
int g_nRep = -1;      // the replay picked
int g_nRepFirst;      // the first row shown
int g_nSeat;          // the person picked, in its people
std::string g_repPicOf;
MapPicture g_repPic;
bool g_bRepPic;

std::vector<TrainerSaved> g_saved;
int g_nResult;
bool g_bPending;
TrainerSetup g_pending;
std::string g_note;
Uint64 g_nNoteAt;
std::string g_justSaved;  // the session saved as the last one ended, marked in the list
Uint64 g_nLastClick;
int g_nLastClickRow = -1;

void Note(const std::string &s)
{
  g_note = s;
  g_nNoteAt = SDL_GetTicks();
}

// ---------------------------------------------------------------------------
// The replays.

std::string Field(const unsigned char *p, size_t n)
{
  size_t i = 0;
  while (i < n && p[i] != 0)
    ++i;
  return std::string(reinterpret_cast<const char *>(p), i);
}

void WalkReplays(const std::string &dir, int nDepth)
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
      WalkReplays(dir + name + "\\", nDepth + 1);
      continue;
    }
    const size_t nDot = name.rfind('.');
    if (nDot == std::string::npos || lstrcmpiA(name.c_str() + nDot, ".rep") != 0)
      continue;
    RepRow r;
    r.path = dir + name;
    r.name = name.substr(0, nDot);
    r.when = fd.ftLastWriteTime;
    g_reps.push_back(r);
  } while (FindNextFileA(h, &fd));
  FindClose(h);
}

void ListReplays(void)
{
  g_reps.clear();
  char sz[MAX_PATH] = "";
  SFileGetUserDataPath(sz, sizeof sz, 1);
  std::string root = sz;
  if (!root.empty() && root.back() != '\\')
    root += '\\';
  WalkReplays(root + "maps\\replays\\", 0);
  std::sort(g_reps.begin(), g_reps.end(), [](const RepRow &a, const RepRow &b) { return CompareFileTime(&a.when, &b.when) > 0; });
  if (g_nRep >= static_cast<int>(g_reps.size()))
    g_nRep = -1;
}

// A replay's header: its map, length and people, and whether it plays in this build.
RepRow &ReadRep(int i)
{
  RepRow &r = g_reps[i];
  if (r.bTried)
    return r;
  r.bTried = true;
  ReplayHxInfo info;
  r.bOk = ReplayHxPeek(r.path.c_str(), &info, NULL);
  if (!r.bOk) {
    r.why = PortText("not a replay Hyperion reads");
    return r;
  }
  const unsigned char *h = info.abHead;
  r.bHx = info.bHx;
  r.dwFrames = *reinterpret_cast<const DWORD *>(h + 0x01);
  r.map = Field(h + 0x61, 0x20);
  const unsigned char *pSlots = h + 0xA1;
  for (int s = 0; s < 12; ++s) {
    const unsigned char *p = pSlots + s * 36;
    if (p[8] == 1)
      ++r.nComputers;
    else if (p[8] == 2)
      r.people.push_back({s, Field(p + 11, 25), p[9] < 3 ? p[9] : -1});
  }
  const bool bMission = *reinterpret_cast<const WORD *>(h + 0x05) != 0;
  if (h[0] != 0 && !IsBroodwar())
    r.why = PortText("a Brood War game, and Brood War is not installed");
  else if (!info.refusal.empty())
    // {0} what the replay was recorded with
    r.why = PortTextF("recorded with {0}", {info.refusal});
  else if (bMission)
    r.why = PortText("a campaign mission");
  else if (r.people.empty())
    r.why = PortText("no person played in it");
  r.bPlays = r.why.empty();
  return r;
}

// the person to take over as: the profile's name, else the first
int SeatFor(const RepRow &r)
{
  for (size_t i = 0; i < r.people.size(); ++i)
    if (lstrcmpiA(r.people[i].name.c_str(), gLocalPlayerName) == 0)
      return static_cast<int>(i);
  return 0;
}

void PickRep(int i)
{
  g_nRep = i;
  if (i >= 0)
    g_nSeat = SeatFor(ReadRep(i));
}

void ShowRepMap(void)
{
  if (g_nRep < 0) {
    g_bRepPic = false;
    return;
  }
  const RepRow &r = g_reps[g_nRep];
  if (g_repPicOf == r.path)
    return;
  g_repPicOf = r.path;
  g_bRepPic = false;
  ReplayHxInfo info;
  std::vector<unsigned char> chk;
  if (ReplayHxPeek(r.path.c_str(), &info, &chk) && !chk.empty()) {
    g_repPic = MapPicture();
    g_repPic.nSlot = kPictureSlot;
    MapPictureRead(&g_repPic, r.path, chk.data(), static_cast<DWORD>(chk.size()));
    g_bRepPic = true;
  }
}

std::string RepClock(unsigned dwFrames)
{
  return PortTrainerTime(dwFrames);
}

// ---------------------------------------------------------------------------
// The set-up.

void Start(void)
{
  if (g_e.nSource == 1) {
    if (g_nRep < 0 || !ReadRep(g_nRep).bPlays) {
      Note(g_nRep < 0 ? PortText("Pick a replay.") : PortText("That replay does not play in this build."));
      return;
    }
    const RepRow &r = g_reps[g_nRep];
    const Person &who = r.people[SDL_clamp(g_nSeat, 0, static_cast<int>(r.people.size()) - 1)];
    TrainerSetup s;
    s.name = r.name;
    s.replay = r.path;
    s.nTakeSlot = who.nSlot;
    s.map.clear();
    s.mapName = r.map;
    s.race = who.nRace >= 0 ? kRaceIds[who.nRace] : "random";
    s.nComputers = r.nComputers;
    s.nSpeed = g_e.nSpeed;
    s.bPointsEachMinute = g_e.bEachMinute;
    s.bAsk = g_e.bAsk;
    g_pending = s;
    g_bPending = true;
    g_nResult = FRONT_TRAINER_STARTED;
    FrontLog("trainer: replay %s as %s (slot %d)\n", r.path.c_str(), who.name.c_str(), who.nSlot);
  }
}

// Start Session on the skirmish screen: its set-up, checked as Start Game checks it, as the
// session's game.
void HostStart(void)
{
  SkirmishGame g;
  if (!SkirmishGameNow(&g))
    return;
  TrainerSetup s;
  s.bGame = true;
  s.game = g;
  s.map = g.map;
  s.mapName = g.title;
  const int nMine = g.seats[0].nRace;
  s.race = nMine >= 0 && nMine <= 2 ? kRaceIds[nMine] : "random";
  s.nComputers = static_cast<int>(g.seats.size()) - 1;
  s.races.clear();
  for (size_t i = 1; i < g.seats.size(); ++i)
    s.races += g.seats[i].nRace >= 0 && g.seats[i].nRace <= 2 ? kRaceLetters[g.seats[i].nRace] : 'r';
  s.nSpeed = g.nSpeed;
  s.nRules = g.bHyperion ? 1 : 0;
  s.bPointsEachMinute = g_e.bEachMinute;
  s.bAsk = g_e.bAsk;
  // {0} the map's name, {1} how many computers
  s.name = PortTextNF("{0}, {1} computer", "{0}, {1} computers", s.nComputers,
                      {g.title, std::to_string(s.nComputers)});
  g_pending = s;
  g_bPending = true;
  g_nResult = FRONT_TRAINER_STARTED;
  FrontLog("trainer: start %s, %s, %d seats, speed %d, %s rules\n", g.map.c_str(), g.type.c_str(),
           static_cast<int>(g.seats.size()), g.nSpeed, g.bHyperion ? "Hyperion" : "classic");
}

void OpenSaved(int i)
{
  if (i < 0 || i >= static_cast<int>(g_saved.size()))
    return;
  const TrainerSaved &s = g_saved[i];
  const std::string &from = s.setup.replay.empty() ? s.setup.map : s.setup.replay;
  if (GetFileAttributesA(from.c_str()) == INVALID_FILE_ATTRIBUTES) {
    Note(s.setup.replay.empty() ? PortTextF("Its map is not where it was: {0}", {from})
                                : PortTextF("Its replay is not where it was: {0}", {from}));
    return;
  }
  g_pending = s.setup;
  g_bPending = true;
  g_nResult = FRONT_TRAINER_STARTED;
  FrontLog("trainer: open %s\n", s.path.c_str());
}

std::string SourceLine(const TrainerSetup &s)
{
  std::string race = s.race;
  if (!race.empty())
    race[0] = static_cast<char>(toupper(static_cast<unsigned char>(race[0])));
  race = PortText(race.c_str());  // one of kRaceNames, or a mod's race
  // {0} the race taken over, {1} how many computers
  const std::string num = std::to_string(s.nComputers);
  if (!s.replay.empty())
    return PortText("From a replay") + std::string(kDot) + s.mapName + kDot + PortTextF("taken over as {0}", {race});
  if (s.bGame)
    return s.mapName + kDot + s.game.type + kDot +
           PortTextNF("{0} against {1} computer", "{0} against {1} computers", s.nComputers, {race, num});
  if (s.nComputers == 1)
    return s.mapName + kDot +
           PortTextNF("{0} against {1} computer", "{0} against {1} computers", s.nComputers, {race, num});
  return s.mapName + kDot +
         (s.bFfa ? PortTextF("{0} against {1} computers, each for itself", {race, num})
                 : PortTextF("{0} against {1} computers, allied", {race, num}));
}

// The view the header's buttons pick: 0 a skirmish, 1 a replay, 2 the sessions.
void PickView(int i)
{
  if (i == 2) {
    g_e.nTab = 1;
    return;
  }
  g_e.nTab = 0;
  g_e.nSource = i;
  if (i == 1) {
    g_repPicOf.clear();
    if (g_nRep < 0 && !g_reps.empty())
      PickRep(0);
  }
}

// ---------------------------------------------------------------------------
// Drawing pieces.

// A card to pick: its name and a line under it; true when clicked.
bool Card(const char *pszId, int i, const char *pszName, const char *pszLine, bool bOn, bool bOff)
{
  Decl c = Box(pszId, i);
  c.d.layout.layoutDirection = CLAY_TOP_TO_BOTTOM;
  c.d.layout.sizing.width = Grow();
  c.d.layout.sizing.height = Grow();
  c.d.layout.padding = Pad(Fs(14), Fs(14), Fs(12), Fs(12));
  c.d.layout.childGap = Gap(Fs(6));
  const bool bHot = !bOff && Over(c.d.id);
  c.d.backgroundColor = Col(bHot ? kLbItemHi : kLbItem);
  c.d.border = Border(bOn ? kGoldRim : bOff ? KitRgb(0x3980bc, 0.2f) : kLbItemRim, Fs(2), Fs(2), Fs(2), Fs(2));
  Radius(c, 4);
  const bool bClicked = !bOff && Clicked(c.d.id);
  Open(c);
  Text(pszName, UI_FACE_BOLD, Fs(19), 0.0f, bOff ? kScInkOff : bOn ? kTrGold : kLbInkInfo);
  Text(pszLine, UI_FACE_TEXT, Fs(15), 0.0f, bOff ? kScInkOff : kInkPlayer, -1, 1.3f);
  Close();
  return bClicked;
}

// ---------------------------------------------------------------------------
// New Session.

void ReplaysBox(float x, float y, float w, float h)
{
  Decl box = InBox("trreplays", x, y, w, h, 10);
  box.d.clip.vertical = true;
  Open(box);
  BoxTitle(PortText("Your Replays"));
  Decl well = Box("trrepwell");
  well.d.layout.layoutDirection = CLAY_TOP_TO_BOTTOM;
  well.d.layout.sizing.width = Grow();
  well.d.layout.sizing.height = Grow();
  well.d.layout.childGap = Gap(Fs(4));
  well.d.clip.vertical = true;
  const bool bOver = Over(well.d.id);
  Open(well);
  if (g_reps.empty())
    Text(PortText("No replays in the data folder's maps\\replays. A game you play is kept there as LastReplay."),
         UI_FACE_TEXT, Fs(16), 0.0f, kLbInkDesc);
  const int nFit = SDL_max(1, static_cast<int>((h - 230) / 62));
  g_nRepFirst += WheelRows(bOver, 1);  // its rows are tall
  g_nRepFirst = SDL_clamp(g_nRepFirst, 0, SDL_max(0, static_cast<int>(g_reps.size()) - nFit));
  for (int i = g_nRepFirst; i < static_cast<int>(g_reps.size()) && i < g_nRepFirst + nFit; ++i) {
    const RepRow &r = ReadRep(i);
    Decl row = Box("trrep", i);
    const bool bOn = i == g_nRep;
    const bool bHot = r.bPlays && Over(row.d.id);
    if (r.bPlays && Clicked(row.d.id))
      PickRep(i);
    row.d.layout.sizing.width = Grow();
    row.d.layout.sizing.height = Fixed(Sc(58));
    row.d.layout.padding = Pad(Fs(14), Fs(14), Fs(6), Fs(6));
    row.d.layout.childGap = Gap(Fs(12));
    row.d.layout.childAlignment.y = CLAY_ALIGN_Y_CENTER;
    row.d.backgroundColor = Col(bOn || bHot ? kLbItemHi : kLbItem);
    row.d.border = Border(bOn ? kGoldRim : kNone, Fs(2), Fs(2), Fs(2), Fs(2));
    Radius(row, 3);
    Open(row);
    Decl a;
    a.d.layout.layoutDirection = CLAY_TOP_TO_BOTTOM;
    a.d.layout.sizing.width = Grow();
    a.d.layout.childGap = Gap(Fs(3));
    Open(a);
    Text(Keep(r.name), UI_FACE_BOLD, Fs(17), 0.0f, !r.bPlays ? kScInkOff : bOn ? kTrGold : kLbInkInfo);
    std::string people;
    for (const Person &p : r.people)
      people += (people.empty() ? "" : ", ") + p.name;
    std::string facts = std::string(PortText("Unreadable"));
    if (r.bOk) {
      facts = r.map + kDot + RepClock(r.dwFrames) + kDot + (people.empty() ? std::string(PortText("no people")) : people);
      if (r.nComputers)
        facts += kDot + PortTextNF("{0} computer", "{0} computers", r.nComputers, {std::to_string(r.nComputers)});
    }
    Text(Keep(facts), UI_FACE_TEXT, Fs(14), 0.0f, r.bPlays ? kInkPlayer : kScInkOff);
    Close();
    // {0} why it does not play here
    Text(Keep(r.bPlays ? std::string(r.bHx ? PortText("Hyperion rules") : PortText("classic")) + kDot +
                             PortText("plays here")
                       : PortTextF("does not play: {0}", {r.why})),
         UI_FACE_TEXT, Fs(13), 0.0f, r.bPlays ? kLbInkDesc : kLbWarn);
    Close();
  }
  Close();
  // who to take over as
  if (g_nRep >= 0 && ReadRep(g_nRep).bPlays) {
    const RepRow &r = g_reps[g_nRep];
    Open(OptionRow(48));
    OptionName(PortText("Take over as"), PortText("the person whose commands become yours"), 6);
    std::vector<std::string> items;
    for (const Person &p : r.people)
      items.push_back(p.name + " (" + PortText(p.nRace >= 0 ? kRaceNames[p.nRace] : kRaceNames[3]) + ")");
    const int n = Pulldown("trseat", 0, 320, items, SDL_clamp(g_nSeat, 0, static_cast<int>(items.size()) - 1),
                           items.size() > 1 ? PD_ON : PD_LOCKED);
    if (n >= 0)
      g_nSeat = n;
    Close();
  }
  Text(PortText("Taking over is an order of yours, or Ctrl+T: from that frame the replay's commands for that player stop "
                "and yours begin, as a new attempt. The computers keep playing by their scripts, the other people as "
                "recorded. The replay itself never changes."),
       UI_FACE_TEXT, Fs(14), 0.0f, kScInkOff, -1, 1.3f);
  Close();
}

void MapBox(float x, float y, float w, float h)
{
  {
    ShowRepMap();
    Decl box = At("trmapbox", x, y, w, h);
    box.d.layout.layoutDirection = CLAY_TOP_TO_BOTTOM;
    box.d.layout.padding = Pad(Fs(11), Fs(11), Fs(10), Fs(10));
    box.d.layout.childGap = Gap(Fs(6));
    InnerBox(box);
    Open(box);
    const float fSide = fminf(w - 26.0f, h - 92.0f);
    const std::string title = g_nRep >= 0 ? g_reps[g_nRep].map : std::string(PortText("No replay"));
    MapPreview("trmappic", g_bRepPic ? &g_repPic : NULL, g_bRepPic ? g_repPic.tileset : -1, Keep(title), g_repPic.w,
               g_repPic.h, fSide, fSide, 30, false, false, NULL);
    Text(Keep(title), UI_FACE_BOLD, Fs(22), 0.0f, kLbInkInfo);
    Text(PortText("The replay's map"), UI_FACE_TEXT, Fs(14), 0.0f, kScInkOff);
    Close();
  }
}

void KeysBox(float x, float y, float w, float h)
{
  Open(InBox("trkeys", x, y, w, h, 10));
  BoxTitle(PortText("In a Session"));
  static const char *const kKeys[][2] = {
      {"Ctrl+Space", PT_("Pause: the game goes into a frame, the timeline round it")},
      {"Ctrl+R", PT_("Retry from the retry point, at once")},
      {"Ctrl+Q", PT_("Set a retry point here")},
      {"Ctrl+K", PT_("Keep this attempt as a timeline")},
      {"Ctrl+B", PT_("Branch: keep the path so far, go on as a new timeline")},
      {"Ctrl+M", PT_("Mark this moment")},
  };
  for (const auto &k : kKeys) {
    Decl row;
    row.d.layout.sizing.width = Grow();
    row.d.layout.childGap = Gap(Fs(10));
    row.d.layout.childAlignment.y = CLAY_ALIGN_Y_CENTER;
    Open(row);
    Decl chip;
    chip.d.layout.sizing.width = Fixed(Sc(96));
    chip.d.layout.padding = Pad(Fs(6), Fs(6), Fs(2), Fs(2));
    chip.d.layout.childAlignment.x = CLAY_ALIGN_X_CENTER;
    chip.d.backgroundColor = Col(kLbInner);
    chip.d.border = Border(kLbInnerRim, 1, 1, 1, 1);
    Radius(chip, 3);
    Open(chip);
    Text(k[0], UI_FACE_BOLD, Fs(13), 0.0f, kLbInkInfo);
    Close();
    Decl what;
    what.d.layout.sizing.width = Grow();
    Open(what);
    Text(PortText(k[1]), UI_FACE_TEXT, Fs(14), 0.0f, kLbInkDesc);
    Close();
    Close();
  }
  Text(PortText("Paused, the Tape's keys are the same letters alone."), UI_FACE_TEXT, Fs(13), 0.0f, kScInkOff);
  Close();
}

void NewSession(float fW, float fH)
{
  const float right = 34, info = 425, gap = 10;
  const float leftW = fW - right - info - gap - 13;
  const float low = fH - 33 - 52 - 14;
  ReplaysBox(13, 93, leftW, low - 93);
  const float mapH = fminf(470.0f, low - 93 - gap - 220);
  MapBox(fW - right - info, 93, info, mapH);
  KeysBox(fW - right - info, 93 + mapH + gap, info, low - 93 - mapH - gap);
  Decl foot = At("trfoot", 40, fH - 33 - 52, fW - 40 - right, 52);
  foot.d.layout.childGap = Gap(Fs(16));
  foot.d.layout.childAlignment.y = CLAY_ALIGN_Y_CENTER;
  Open(foot);
  Decl fine;
  fine.d.layout.sizing.width = Grow();
  Open(fine);
  Text(PortText("The replay is the session's first line: a snapshot every 5 seconds as it plays, so any moment of it is a "
                "short seek away."),
       UI_FACE_TEXT, Fs(15), 0.0f, kLbInkDesc);
  Close();
  Decl b = Box("trback");
  b.d.layout.sizing.width = Fixed(Sc(180));
  b.d.layout.sizing.height = Fixed(Sc(52));
  if (ScButton(b, PortText("Back"), 'B', false))
    g_nResult = FRONT_TRAINER_HOME;
  Decl s = Box("trgo");
  s.d.layout.sizing.width = Fixed(Sc(300));
  s.d.layout.sizing.height = Fixed(Sc(52));
  if (ScButton(s, PortText("Watch Replay"), 'G', true))
    Start();
  Close();
}

// ---------------------------------------------------------------------------
// Sessions.

// A saved session's shape, small: its lines as rows, each from its split to its end.
void DrawShape(float x, float y, float w, float h, void *pCtx)
{
  const ScDraw &d = *static_cast<const ScDraw *>(pCtx);
  if (d.n < 0 || d.n >= static_cast<int>(g_saved.size()))
    return;
  const TrainerSaved &s = g_saved[d.n];
  UiFill(x, y, w, h, KitRgb(0x020a14, 0.6f), Sc(3));
  const float fLong = static_cast<float>(SDL_max(1u, s.nLongest));
  const int nRows = static_cast<int>(s.shape.size());
  const float pad = Sc(8), rowH = nRows > 1 ? (h - 2 * pad) / static_cast<float>(nRows - 1) : 0.0f;
  int nKept = 0;
  for (int i = 0; i < nRows; ++i) {
    const TrainerLine &l = s.shape[i];
    const float ly = nRows > 1 ? y + pad + rowH * static_cast<float>(i) : y + h / 2.0f;
    const float x0 = x + pad + (w - 2 * pad) * static_cast<float>(l.nSplit) / fLong;
    const float x1 = x + pad + (w - 2 * pad) * static_cast<float>(l.nEnd) / fLong;
    const bool bAttempt = l.nKind == TRAINER_ATTEMPT;
    const UiColor c = bAttempt ? kAttempt : KitRgb(kLineColours[nKept++ % 6]);
    // from its parent's row at the split
    for (int k = 0; k < i; ++k)
      if (s.shape[k].nId == l.nParent) {
        const float py = nRows > 1 ? y + pad + rowH * static_cast<float>(k) : ly;
        UiLine(x0, py, x0 + Sc(6), ly, c, Sc(1.5f));
        break;
      }
    UiLine(x0 + (l.nParent ? Sc(6) : 0.0f), ly, SDL_max(x1, x0 + Sc(8)), ly, c, Sc(bAttempt ? 1.5f : 2.5f));
  }
}

void SessionsList(float x, float y, float w, float h)
{
  Decl box = InBox("trsessions", x, y, w, h, 10);
  box.d.clip.vertical = true;
  Open(box);
  BoxTitle(PortText("Sessions"));
  if (g_saved.empty()) {
    Text(PortText("No sessions yet. Leaving a session asks whether to save it; the game's own end saves it."),
         UI_FACE_TEXT, Fs(17), 0.0f, kLbInkDesc);
  }
  const int nFit = SDL_max(1, static_cast<int>((h - 120) / 82));
  const int nFirst = g_e.nPick >= nFit ? g_e.nPick - nFit + 1 : 0;
  for (int i = nFirst; i < static_cast<int>(g_saved.size()) && i < nFirst + nFit; ++i) {
    const TrainerSaved &s = g_saved[i];
    Decl row = Box("trses", i);
    const bool bOn = i == g_e.nPick;
    const bool bHot = Over(row.d.id);
    if (Clicked(row.d.id)) {
      const Uint64 nNow = SDL_GetTicks();
      const bool bDouble = g_nLastClickRow == i && nNow - g_nLastClick < 450;
      g_nLastClick = nNow;
      g_nLastClickRow = i;
      g_e.nPick = i;
      if (bDouble)
        OpenSaved(i);
    }
    row.d.layout.sizing.width = Grow();
    row.d.layout.sizing.height = Fixed(Sc(74));
    row.d.layout.padding = Pad(Fs(12), Fs(16), Fs(8), Fs(8));
    row.d.layout.childGap = Gap(Fs(16));
    row.d.layout.childAlignment.y = CLAY_ALIGN_Y_CENTER;
    row.d.backgroundColor = Col(bOn || bHot ? kLbItemHi : kLbItem);
    row.d.border = Border(bOn ? kGoldRim : kNone, Fs(2), Fs(2), Fs(2), Fs(2));
    Radius(row, 3);
    Open(row);
    Decl shape;
    shape.d.layout.sizing.width = Fixed(Sc(200));
    shape.d.layout.sizing.height = Fixed(Sc(56));
    shape.d.custom.customData = Custom(DrawShape, kScButton, 0.0f, i);
    Open(shape);
    Close();
    Decl a;
    a.d.layout.layoutDirection = CLAY_TOP_TO_BOTTOM;
    a.d.layout.sizing.width = Grow();
    a.d.layout.childGap = Gap(Fs(4));
    Open(a);
    Decl nm;
    nm.d.layout.childGap = Gap(Fs(10));
    nm.d.layout.childAlignment.y = CLAY_ALIGN_Y_CENTER;
    Open(nm);
    Text(Keep(s.name), UI_FACE_BOLD, Fs(18), 0.0f, bOn ? kTrGold : kLbInkInfo);
    if (!g_justSaved.empty() && lstrcmpiA(s.path.c_str(), g_justSaved.c_str()) == 0) {
      Decl tag;
      tag.d.layout.padding = Pad(Fs(8), Fs(8), Fs(2), Fs(2));
      tag.d.border = Border(kGoldRim, 1, 1, 1, 1);
      Radius(tag, 3);
      Open(tag);
      Text(PortText("JUST SAVED"), UI_FACE_WIDE, Fs(11), 0.04f, kTrGold);
      Close();
    }
    Close();
    std::string facts = SourceLine(s.setup) + kDot +
                        PortTextNF("{0} timeline", "{0} timelines", s.nTimelines, {std::to_string(s.nTimelines)});
    if (s.nAttempts > 0)
      facts += ", " + PortTextNF("{0} try", "{0} tries", s.nAttempts, {std::to_string(s.nAttempts)});
    // {0} how long the longest line is
    facts += kDot + PortTextF("the longest {0}", {PortTrainerTime(s.nLongest)});
    Text(Keep(facts), UI_FACE_TEXT, Fs(15), 0.0f, kInkPlayer);
    if (s.bOtherBuild)
      Text(PortText("Made by a build with other rules: it may play differently here."), UI_FACE_TEXT, Fs(13), 0.0f,
           kLbWarn);
    Close();
    Text(Keep(s.when), UI_FACE_TEXT, Fs(15), 0.0f, kLbInkDesc);
    Close();
  }
  Spacer();
  Text(PortText("A session is the game it started from (its settings and seed, or a copy of the replay), its lines as "
                "commands, its retry points and marks. Snapshots are not saved: they are made again when it opens."),
       UI_FACE_TEXT, Fs(14), 0.0f, kScInkOff, -1, 1.3f);
  Close();
}

void Sessions(float fW, float fH)
{
  const float right = 34;
  SessionsList(13, 93, fW - 13 - right, fH - 93 - 33 - 52 - 14);
  Decl foot = At("trsfoot", 40, fH - 33 - 52, fW - 40 - right, 52);
  foot.d.layout.childGap = Gap(Fs(16));
  foot.d.layout.childAlignment.y = CLAY_ALIGN_Y_CENTER;
  Open(foot);
  Decl fine;
  fine.d.layout.sizing.width = Grow();
  Open(fine);
  Text(PortText("Opened, a session's game is made again and paused at its start with the Tape up: any line, any moment."),
       UI_FACE_TEXT, Fs(15), 0.0f, kLbInkDesc);
  Close();
  Decl b = Box("trsback");
  b.d.layout.sizing.width = Fixed(Sc(180));
  b.d.layout.sizing.height = Fixed(Sc(52));
  if (ScButton(b, PortText("Back"), 'B', false))
    g_nResult = FRONT_TRAINER_HOME;
  Decl s = Box("trsopen");
  s.d.layout.sizing.width = Fixed(Sc(300));
  s.d.layout.sizing.height = Fixed(Sc(52));
  if (ScButton(s, PortText("Open Session"), 'G', true, !g_saved.empty()))
    OpenSaved(g_e.nPick);
  Close();
}

// The header's buttons, on the skirmish screen and here: where a session starts, and the
// sessions saved.
void HeadButtons(void)
{
  const char *const kNames[3] = {PortText("A Skirmish"), PortText("A Replay"), PortText("Sessions")};
  for (int i = 0; i < 3; ++i) {
    const bool bOn = i == 2 ? g_e.nTab == 1 : g_e.nTab == 0 && g_e.nSource == i;
    Decl b = Box("trview", i);
    b.d.layout.sizing.width = Fit();
    b.d.layout.sizing.width.size.minMax.min = Sc(150);
    b.d.layout.sizing.height = Fixed(Sc(40));
    if (bOn)
      b.d.border = Border(kGoldRim, Fs(2), Fs(2), Fs(2), Fs(2));
    const std::string label = i == 2 ? std::string(kNames[i]) + kDot + std::to_string(g_saved.size()) : kNames[i];
    if (ScMini(b, Keep(label), static_cast<char>('1' + i)))
      PickView(i);
  }
  TipOn(Id("trview", 0), PortText("A skirmish"),
        PortText("You against computers, set up as Skirmish sets one up. It records as you play, and you can go back to any "
                 "moment of it. Hotkey: 1"));
  TipOn(Id("trview", 1), PortText("A replay"),
        PortText("Watch one of yours, and take over from any moment: from there the commands are yours. Hotkey: 2"));
}

bool HostKeys(void)
{
  for (int i = 0; i < 3; ++i)
    if (Key(static_cast<unsigned>('1' + i))) {
      PickView(i);
      return true;
    }
  return false;
}

void HostBack(void)
{
  g_nResult = FRONT_TRAINER_HOME;
}

// A Practice card, as the mutators' cards: its name and pulldown, a line under them.
void PracticeCard(int i, float w, const char *pszName, const std::vector<std::string> &items, int nCur,
                  const char *pszLine, int *pnChosen)
{
  Decl card = Box("trpcard", i);
  card.d.layout.layoutDirection = CLAY_TOP_TO_BOTTOM;
  card.d.layout.sizing.width = Fixed(Sc(w));
  card.d.layout.padding = Pad(Fs(12), Fs(12), Fs(9), Fs(10));
  card.d.layout.childGap = Gap(Fs(6));
  card.d.backgroundColor = Col(kLbItem);
  card.d.border = Border(kLbItemRim, 1, 1, 1, 1);
  Radius(card, 3);
  Open(card);
  Decl top;
  top.d.layout.sizing.width = Grow();
  top.d.layout.childGap = Gap(Fs(8));
  top.d.layout.childAlignment.y = CLAY_ALIGN_Y_CENTER;
  Open(top);
  Decl nm;
  nm.d.layout.sizing.width = Grow();
  Open(nm);
  Text(pszName, UI_FACE_BOLD, Fs(16), 0.0f, kScInkLabel);
  Close();
  *pnChosen = Pulldown("trpdd", i, 220, items, nCur);
  Close();
  Text(pszLine, UI_FACE_TEXT, Fs(13), 0.0f, kLbInkDesc, -1, 1.3f);
  Close();
}

// The Practice box, in the mutators' place on the skirmish screen: the session's own options.
void PracticeBox(float x, float y, float w, float h)
{
  Decl box = At("trpractice", x, y, w, h);
  box.d.layout.layoutDirection = CLAY_TOP_TO_BOTTOM;
  box.d.layout.padding = Pad(Fs(20), Fs(20), Fs(14), Fs(14));
  box.d.layout.childGap = Gap(Fs(12));
  box.d.clip.vertical = true;
  InnerBox(box);
  Open(box);
  Decl head;
  head.d.layout.sizing.width = Grow();
  head.d.layout.childGap = Gap(Fs(14));
  head.d.layout.childAlignment.y = CLAY_ALIGN_Y_CENTER;
  Open(head);
  Text(PortText("PRACTICE"), UI_FACE_WIDE, Fs(16), 0.04f, kScInkHead);
  Spacer();
  Text(PortText("Every line saved as a .hxtrain when you leave, if you choose"), UI_FACE_TEXT, Fs(13), 0.0f,
       kLbInkDesc);
  Close();
  Decl line;
  line.d.layout.childGap = Gap(Fs(8));
  Open(line);
  const float fCard = (w - 40 - 8) / 2;
  int n = -1;
  PracticeCard(0, fCard, PortText("Retry points"),
               {PortText("Yours (Ctrl+Q)"), PortText("Yours and each minute")}, g_e.bEachMinute ? 1 : 0,
               PortText("The start is always one; Ctrl+R goes back to the latest."), &n);
  if (n >= 0)
    g_e.bEachMinute = n == 1;
  n = -1;
  PracticeCard(1, fCard, PortText("Going back"), {PortText("Ask first"), PortText("Always try again")},
               g_e.bAsk ? 0 : 1,
               PortText("Going on from a line's middle: ask the four ways on, or try again at once."), &n);
  if (n >= 0)
    g_e.bAsk = n == 0;
  Close();
  Close();
}

const SkirmishHost g_host = {
    "trainer", PT_("Trainer, a new session"), PT_("Start Session"),
    PT_("The players, the game options and the practice come next, then Start Session."),
    HeadButtons, PracticeBox, HostStart, HostKeys, HostBack};

}  // namespace

namespace FrontKit {

void TrainerOpen(bool bSessions)
{
  g_nResult = 0;
  g_bPending = false;
  g_e.nTab = bSessions ? 1 : 0;
  // the skirmish screen, hosting the trainer's set-up, which it keeps apart from Skirmish's
  SkirmishOpen(g_e.nSpeed, &g_host);
  g_saved = PortTrainerSessions();
  g_justSaved = bSessions ? PortTrainerLastSaved() : std::string();
  ListReplays();
  g_repPicOf.clear();
  if (g_nRep < 0 && !g_reps.empty())
    PickRep(0);
  g_e.nPick = SDL_clamp(g_e.nPick, 0, SDL_max(0, static_cast<int>(g_saved.size()) - 1));
  if (bSessions)
    g_e.nPick = 0;
  g_note.clear();
  FrontLog("trainer: open, %u sessions, %u replays\n", static_cast<unsigned>(g_saved.size()),
           static_cast<unsigned>(g_reps.size()));
}

bool TrainerCameBack(void)
{
  return PortTrainerCameBack();
}

bool TrainerTake(TrainerSetup *pOut)
{
  if (!g_bPending)
    return false;
  g_bPending = false;
  *pOut = g_pending;
  return true;
}

int TrainerResult(void)
{
  const int n = g_nResult;
  g_nResult = 0;
  return n;
}

void TrainerScreen(void)
{
  if (g_e.nTab == 0 && g_e.nSource == 0) {
    SkirmishScreen();
    return;
  }
  MapPreviewFrame();
  const float fW = UiWidth() / Sc(1) - 116;
  const float fH = fminf(990.0f, UiHeight() / Sc(1) - 132 - 74);
  Decl panel = Box("trpanel");
  panel.d.layout.sizing.width = Fixed(Sc(fW));
  panel.d.layout.sizing.height = Fixed(Sc(fH));
  panel.d.floating.attachTo = CLAY_ATTACH_TO_ROOT;
  panel.d.floating.offset.x = Sc(58);
  panel.d.floating.offset.y = Sc(132);
  panel.d.floating.zIndex = 20;
  static UiCustom frame = {0.0f, DrawScFrame, NULL};
  panel.d.custom.customData = &frame;
  Open(panel);
  Decl head = At("trhead", 40, 22, fW - 40 - 620, 0);
  head.d.layout.layoutDirection = CLAY_TOP_TO_BOTTOM;
  Open(head);
  Text(PortText("Trainer"), UI_FACE_BOLD, Fs(27), 0.0f, kInkWhite);
  const bool bNote = !g_note.empty() && SDL_GetTicks() - g_nNoteAt < 6000;
  Text(bNote ? Keep(g_note)
             : Keep(std::string(PortText("A proof of concept, a work in progress")) + kDot +
                    PortText("go back to any moment and try it again") + kDot +
                    PortText("timelines and their paths")),
       UI_FACE_TEXT, Fs(17), 0.0f, bNote ? kTrGold : kLbInkInfo);
  Close();
  Decl tabs = Floating("trtabs", 0, -Sc(34), Sc(32), 22, CLAY_ATTACH_POINT_RIGHT_TOP, CLAY_ATTACH_POINT_RIGHT_TOP);
  tabs.d.layout.childGap = Gap(Fs(8));
  Open(tabs);
  HeadButtons();
  Close();
  if (g_e.nTab == 0)
    NewSession(fW, fH);
  else
    Sessions(fW, fH);
  Close();
  DrawTip();
  PulldownCloseOutside();
}

bool TrainerKeys(void)
{
  if (g_e.nTab == 0 && g_e.nSource == 0)
    return SkirmishKeys();
  if (PulldownKeys())
    return true;
  if (HostKeys())
    return true;
  if (Key('G') && g_e.nTab == 0)
    Start();
  else if ((Key('G') || Key(VK_RETURN)) && g_e.nTab == 1)
    OpenSaved(g_e.nPick);
  else if (g_e.nTab == 0 && g_e.nSource == 1 && Key(VK_DOWN) && g_nRep + 1 < static_cast<int>(g_reps.size()))
    PickRep(g_nRep + 1);
  else if (g_e.nTab == 0 && g_e.nSource == 1 && Key(VK_UP) && g_nRep > 0)
    PickRep(g_nRep - 1);
  else if (g_e.nTab == 1 && Key(VK_DOWN) && g_e.nPick + 1 < static_cast<int>(g_saved.size()))
    ++g_e.nPick;
  else if (g_e.nTab == 1 && Key(VK_UP) && g_e.nPick > 0)
    --g_e.nPick;
  else if (Key(VK_ESCAPE) || Key('B'))
    g_nResult = FRONT_TRAINER_HOME;
  return true;
}

}  // namespace FrontKit
