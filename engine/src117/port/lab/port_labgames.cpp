// port_labgames.cpp -- the AI lab's games (port_labgames.h). The project's index,
// games.jsonl, is read as it grows, a line a game as the farm writes it when the game ends
// (port_labfarm.cpp), and shown newest first, only the rows that fit drawn. The game picked
// shows its map, its players with their races, levels and armies at the end, how it ended,
// and what can be done with it: its folder opened, its replay copied among the player's own.
#include <windows.h>
#include <math.h>
#include <stdio.h>
#include <algorithm>
#include <string>
#include <vector>
#include "port_labfarm.h"
#include "port_labgames.h"
#include "port_labwall.h"
#include "port_labwatch.h"
#include "../front/port_frontkit.h"
#include "../front/port_frontlobby.h"
#include "../front/port_ui.h"
#include "../gameplay/port_ai.h"
#include "../platform/port_json.h"
#include "../platform/port_paths.h"
#include "../platform/port_util.h"

using namespace FrontKit;

namespace {

struct Seat {
  int nSlot = 0, nRace = -1, nLevel = 0, nUnits = 0, nMinerals = 0, nGas = 0;
  std::string name;
};

struct Row {
  uint32_t dwGame = 0;
  std::string map, mapPath, levels, at, ended, why, state;
  int nPlayers = 0, nSeed = 0, nWorker = 0;
  unsigned dwEndFrame = 0;
  std::vector<int> winners;
  double dSeconds = 0.0, dFps = 0.0;
  std::vector<Seat> seats;
};

std::vector<Row> g_rows;
std::string g_from;   // the index read, and how much of it
size_t g_nRead;
uint64_t g_qReadAt;
int g_nFirst, g_nShown = 1;
uint32_t g_dwPick;    // the game picked, 0 for the newest
std::string g_note;

const char *const kRaceNames[] = {"Zerg", "Terran", "Protoss"};
const char *const kColourNames[8] = {"Red", "Blue", "Teal", "Purple", "Orange", "Brown", "White", "Yellow"};
const float kCols[] = {0.55f, 1.4f, 2.6f, 1.6f, 0.95f, 0.85f, 1.0f};
const int kColCount = 7;

std::string Thousands(long long n)
{
  std::string s = std::to_string(n < 0 ? -n : n);
  for (int i = static_cast<int>(s.size()) - 3; i > 0; i -= 3)
    s.insert(static_cast<size_t>(i), ",");
  return n < 0 ? "-" + s : s;
}

std::string Clock(unsigned dwFrame)
{
  const unsigned n = static_cast<unsigned>(dwFrame * 42ull / 1000);  // Fastest's 42 ms a frame
  char sz[24];
  if (n >= 3600)
    snprintf(sz, sizeof sz, "%u:%02u:%02u", n / 3600, n / 60 % 60, n % 60);
  else
    snprintf(sz, sizeof sz, "%u:%02u", n / 60, n % 60);
  return sz;
}

std::string MapTitle(const std::string &file)
{
  std::string name = file;
  if (name.size() > 3 && name[0] == '(' && name[2] == ')')
    name = name.substr(3);
  return name.substr(0, name.rfind('.'));
}

std::string ColourName(int nSlot)
{
  return nSlot >= 0 && nSlot < 8 ? kColourNames[nSlot] : "Player " + std::to_string(nSlot + 1);
}

std::string RaceName(int nRace)
{
  return nRace >= 0 && nRace < 3 ? kRaceNames[nRace] : "Random";
}

std::string Result(const Row &r)
{
  if (r.ended == "last")
    return r.winners.empty() ? std::string("Nobody left") : ColourName(r.winners[0]) + " wins";
  if (r.ended == "finished")
    return "Length limit";
  if (r.ended == "stopped")
    return "Stopped";
  return "Lost";
}

void Parse(const std::string &line)
{
  PortJson j;
  if (!PortJsonParse(line.c_str(), &j) || j.kind != PortJson::OBJECT)
    return;
  Row r;
  r.dwGame = static_cast<uint32_t>(j["game"].Num());
  r.map = j["map"].Str();
  r.mapPath = j["map_path"].Str();
  r.levels = j["levels"].Str();
  r.at = j["at"].Str();
  r.ended = j["ended"].Str();
  r.why = j["why"].Str();
  r.state = j["state"].Str();
  r.nPlayers = static_cast<int>(j["players"].Num());
  r.nSeed = static_cast<int>(j["seed"].Num());
  r.nWorker = static_cast<int>(j["worker"].Num());
  r.dwEndFrame = static_cast<unsigned>(j["end_frame"].Num());
  for (size_t i = 0; i < j["winners"].size(); ++i)
    r.winners.push_back(static_cast<int>(j["winners"].At(i).Num()));
  r.dSeconds = j["seconds"].Num();
  r.dFps = j["game_fps"].Num();
  const PortJson &seats = j["seats"];
  for (size_t i = 0; i < seats.size(); ++i) {
    const PortJson &s = seats.At(i);
    Seat st;
    st.nSlot = static_cast<int>(s["slot"].Num());
    st.nRace = static_cast<int>(s["race"].Num(-1));
    st.nLevel = static_cast<int>(s["level"].Num());
    st.nUnits = static_cast<int>(s["units"].Num());
    st.nMinerals = static_cast<int>(s["minerals"].Num());
    st.nGas = static_cast<int>(s["gas"].Num());
    st.name = s["name"].Str();
    r.seats.push_back(st);
  }
  g_rows.push_back(r);
}

// What the index has gained since it was last read, a whole line at a time.
void ReadIndex(void)
{
  const uint64_t qNow = PortNowMs();
  const std::string path = PortLabFarmFolder() + "games.jsonl";
  if (path != g_from) {
    g_from = path;
    g_rows.clear();
    g_nRead = 0;
    g_nFirst = 0;
    g_dwPick = 0;
  } else if (qNow - g_qReadAt < 500) {
    return;
  }
  g_qReadAt = qNow;
  FILE *f = fopen(path.c_str(), "rb");
  if (f == NULL)
    return;
  std::string text;
  if (fseek(f, static_cast<long>(g_nRead), SEEK_SET) == 0) {
    char buf[65536];
    for (size_t n; (n = fread(buf, 1, sizeof buf, f)) > 0;)
      text.append(buf, n);
  }
  fclose(f);
  size_t at = 0;
  for (size_t end; (end = text.find('\n', at)) != std::string::npos; at = end + 1) {
    std::string line = text.substr(at, end - at);
    if (!line.empty() && line.back() == '\r')
      line.pop_back();
    if (!line.empty())
      Parse(line);
  }
  g_nRead += at;
}

const Row *Picked(void)
{
  if (g_rows.empty())
    return NULL;
  for (const Row &r : g_rows)
    if (r.dwGame == g_dwPick)
      return &r;
  return &g_rows.back();
}

void Table(float x, float y, float w, float h)
{
  Decl box = At("lgtable", x, y, w, h);
  box.d.layout.layoutDirection = CLAY_TOP_TO_BOTTOM;
  box.d.clip.vertical = true;
  InnerBox(box);
  Open(box);
  float fSum = 0;
  for (float f : kCols)
    fSum += f;
  const float fInner = w - 24 - (kColCount - 1) * 10;
  auto colW = [&](int c) { return Sc(fInner * kCols[c] / fSum); };
  Decl head;
  head.d.layout.sizing.width = Grow();
  head.d.layout.sizing.height = Fixed(Sc(36));
  head.d.layout.padding = Pad(Fs(12), Fs(12), 0, 0);
  head.d.layout.childGap = Gap(Fs(10));
  head.d.layout.childAlignment.y = CLAY_ALIGN_Y_CENTER;
  head.d.backgroundColor = Col(kScMenuFill);
  Open(head);
  static const char *const kHeads[] = {"#", "Map", "Players", "Result", "Game time", "Real time", "Frames/s"};
  for (int c = 0; c < kColCount; ++c) {
    Decl hc;
    hc.d.layout.sizing.width = Fixed(colW(c));
    if (c >= 4)
      hc.d.layout.childAlignment.x = CLAY_ALIGN_X_RIGHT;
    Open(hc);
    Text(Keep(Upper(kHeads[c])), UI_FACE_WIDE, Fs(12), 0.04f, kScInkHead);
    Close();
  }
  Close();
  const int nRows = static_cast<int>(g_rows.size());
  g_nShown = static_cast<int>((h - 36 - 8) / 46);
  if (g_nShown < 1)
    g_nShown = 1;
  g_nFirst += WheelRows(Over(Id("lgtable")));
  const int nMaxFirst = nRows > g_nShown ? nRows - g_nShown : 0;
  g_nFirst = g_nFirst < 0 ? 0 : g_nFirst > nMaxFirst ? nMaxFirst : g_nFirst;
  if (g_rows.empty()) {
    Decl e;
    e.d.layout.padding = Pad(Fs(14), Fs(14), Fs(20), 0);
    Open(e);
    Text("No games yet: start the farm, and each game is listed here as it ends.", UI_FACE_TEXT, Fs(16), 0.0f,
         kLbInkDesc);
    Close();
  }
  const Row *pPick = Picked();
  for (int i = g_nFirst; i < nRows && i < g_nFirst + g_nShown; ++i) {
    const Row &r = g_rows[static_cast<size_t>(nRows - 1 - i)];  // newest first
    Decl row = Box("lgrow", i);
    row.d.layout.sizing.width = Grow();
    row.d.layout.sizing.height = Fixed(Sc(46));
    row.d.layout.padding = Pad(Fs(12), Fs(12), 0, 0);
    row.d.layout.childGap = Gap(Fs(10));
    row.d.layout.childAlignment.y = CLAY_ALIGN_Y_CENTER;
    row.d.border = Border(kLbDivider, 0, 0, 0, 1);
    const bool bPick = &r == pPick;
    row.d.backgroundColor = Col(bPick ? kLbItem : Over(row.d.id) ? kLbItemHi : kNone);
    if (Clicked(row.d.id)) {
      g_dwPick = r.dwGame;
      g_note.clear();
    }
    Open(row);
    std::string players;
    for (const Seat &s : r.seats)
      players += (players.empty() ? "" : " \xc2\xb7 ") + RaceName(s.nRace) + " " + AiLevelName(s.nLevel);
    const bool bEnded = r.ended == "last" || r.ended == "finished";
    const std::string cells[kColCount] = {
        std::to_string(r.dwGame),
        MapTitle(r.map),
        players.empty() ? std::to_string(r.nPlayers) + " players" : players,
        Result(r),
        bEnded ? Clock(r.dwEndFrame) : std::string("\xe2\x80\x94"),
        bEnded ? std::to_string(static_cast<int>(r.dSeconds * 10 + 0.5) / 10) + "." +
                     std::to_string(static_cast<int>(r.dSeconds * 10 + 0.5) % 10) + " s"
               : std::string("\xe2\x80\x94"),
        bEnded ? Thousands(static_cast<long long>(r.dFps + 0.5)) : std::string("\xe2\x80\x94")};
    for (int c = 0; c < kColCount; ++c) {
      Decl cell;
      cell.d.layout.sizing.width = Fixed(colW(c));
      if (c >= 4)
        cell.d.layout.childAlignment.x = CLAY_ALIGN_X_RIGHT;
      Open(cell);
      UiColor ink = kLbInkInfo;
      if (c == 3 && r.ended == "last" && !r.winners.empty())
        ink = KitRgb(r.winners[0] < 8 ? kSlotColours[r.winners[0]] : 0xb0b0b0);
      else if (c == 3 && !bEnded)
        ink = kLbInkDesc;
      Text(Keep(cells[c]), c == 0 ? UI_FACE_BOLD : UI_FACE_TEXT, Fs(15), 0.0f, ink);
      Close();
    }
    Close();
  }
  Close();
}

struct PicCtx {
  std::string path;
};
PicCtx g_pic;

void DrawPic(float x, float y, float w, float h, void *pCtx)
{
  LabMapPicture(x, y, w, h, static_cast<PicCtx *>(pCtx)->path, 1.0f);
}

void Line(const std::string &s, UiColor c = kLbInkInfo, float fSize = 16)
{
  Text(Keep(s), UI_FACE_TEXT, Fs(fSize), 0.0f, c);
}

void Detail(float x, float y, float w, float h)
{
  Decl box = InBox("lgdetail", x, y, w, h, 8);
  Open(box);
  const Row *r = Picked();
  if (r == NULL) {
    BoxTitle("The Game");
    Line("Pick a game in the list.", kLbInkDesc);
    Close();
    return;
  }
  BoxTitle(Keep("Game " + std::to_string(r->dwGame)));
  Text(Keep(MapTitle(r->map)), UI_FACE_BOLD, Fs(20), 0.0f, kScInkHi);
  // the map
  g_pic.path = r->mapPath;
  static UiCustom s_pic;
  s_pic = UiCustom{0.0f, DrawPic, &g_pic};
  Decl pic;
  pic.d.layout.sizing.width = Grow();
  pic.d.layout.sizing.height = Fixed(Sc(w * 0.55f));
  pic.d.custom.customData = &s_pic;
  Open(pic);
  Close();
  // its players
  for (const Seat &s : r->seats) {
    Decl row;
    row.d.layout.childGap = Gap(Fs(8));
    row.d.layout.childAlignment.y = CLAY_ALIGN_Y_CENTER;
    Open(row);
    Decl sw;
    sw.d.layout.sizing.width = Fixed(Sc(12));
    sw.d.layout.sizing.height = Fixed(Sc(12));
    sw.d.backgroundColor = Col(KitRgb(s.nSlot < 8 ? kSlotColours[s.nSlot] : 0xb0b0b0));
    Open(sw);
    Close();
    const bool bWon = std::find(r->winners.begin(), r->winners.end(), s.nSlot) != r->winners.end();
    Line(ColourName(s.nSlot) + kDot + RaceName(s.nRace) + kDot + AiLevelName(s.nLevel) +
             (bWon ? std::string(kDot) + "won" : std::string()),
         bWon ? kScInkHi : kLbInkInfo);
    Close();
    Line("   " + Thousands(s.nUnits) + " units at the end" + kDot + Thousands(s.nMinerals) + " minerals" + kDot +
             Thousands(s.nGas) + " gas gathered",
         kLbInkDesc, 14);
  }
  // how it went
  const bool bEnded = r->ended == "last" || r->ended == "finished";
  if (bEnded)
    Line(Result(*r) + kDot + Clock(r->dwEndFrame) + " of game in " + std::to_string(static_cast<int>(r->dSeconds + 0.5)) +
         " s");
  else
    Line(Result(*r) + (r->why.empty() ? std::string() : ": " + r->why), kLbInkDesc);
  Line("Seed " + std::to_string(r->nSeed) + kDot + "worker " + std::to_string(r->nWorker) + kDot + r->at, kLbInkDesc,
       14);
  if (!r->state.empty())
    Line("State at its end " + r->state, kLbInkDesc, 14);
  Decl fill;
  fill.d.layout.sizing.height = Grow();
  Open(fill);
  Close();
  if (!g_note.empty())
    Line(g_note, kLbInkDesc, 14);
  // what can be done with it
  char szGame[16];
  snprintf(szGame, sizeof szGame, "%06u", r->dwGame);
  const std::string folder = PortLabFarmFolder() + "games\\" + szGame;
  Decl watch = Box("lgwatch");
  watch.d.layout.sizing.width = Grow();
  watch.d.layout.sizing.height = Fixed(Sc(46));
  if (ScButton(watch, "Watch from the Start", 'W', true))
    PortLabWatchAsk(r->dwGame, r->map, &g_note);
  Decl acts;
  acts.d.layout.sizing.width = Grow();
  acts.d.layout.childGap = Gap(Fs(10));
  Open(acts);
  Decl open = Box("lgopen");
  open.d.layout.sizing.width = Grow();
  open.d.layout.sizing.height = Fixed(Sc(40));
  if (ScMini(open, "Open Folder", 0))
    ShellExecuteA(NULL, "open", PortExplorer(), ("\"" + folder + "\"").c_str(), NULL, SW_SHOWNORMAL);
  Decl copy = Box("lgcopy");
  copy.d.layout.sizing.width = Grow();
  copy.d.layout.sizing.height = Fixed(Sc(40));
  if (ScMini(copy, "Copy to My Replays", 0, bEnded)) {
    const std::string maps = std::string(PortDataFolder()) + "maps";
    CreateDirectoryA(maps.c_str(), NULL);
    CreateDirectoryA((maps + "\\replays").c_str(), NULL);
    const std::string lab = maps + "\\replays\\Lab";
    CreateDirectoryA(lab.c_str(), NULL);
    const std::string to = lab + "\\" + PortLabFarmProjectName() + " game " + std::to_string(r->dwGame) + ".rep";
    g_note = CopyFileA((folder + "\\run.rep").c_str(), to.c_str(), FALSE)
                 ? "Copied to your replays, in the Lab folder."
                 : "The replay could not be copied.";
  }
  Close();
  Close();
}

}  // namespace

namespace FrontKit {

void LabGames(float x, float y, float w, float h)
{
  ReadIndex();
  const float fDetail = fminf(470.0f, w * 0.36f), fGap = 10.0f;
  Table(x, y, w - fDetail - fGap, h);
  Detail(x + w - fDetail, y, fDetail, h);
}

}  // namespace FrontKit
