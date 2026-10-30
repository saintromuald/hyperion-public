// port_front_score.cpp -- Hyperion's score screen. Presentation only: the
// game's numbers are port_score.cpp's, read after the game, and OK goes on as 1.17's OK does.
//
// 1.17's placement (rez\gluScore.bin, gluScore.cpp) in StarCraft II's look: the outcome top
// left; Overview, Units, Structures and Resources across the top on V, U, T and R; a row a
// player, the race in a box of their colour, three bars measured against the column's best and
// the tab's score in a box; the elapsed time, Save Replay and OK at the foot. Winners first and
// losers last, and every number counting up by 1.17's rule, with its two sounds. Past 8
// players the list goes compact and past 24 into two columns, as the set-up's slots do; a game
// with teams lists its players, under team bands with their sums, or the teams alone, each
// opening onto its players. A title sorts by its column; a click marks a player on every tab.
#include <windows.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <algorithm>
#include <string>
#include <vector>
#include "port_frontkit.h"
#include "port_frontlobby.h"
#include "port_frontscreens.h"
#include "port_ui.h"
#include "../gameplay/port_ai.h"         // AiLevelName
#include "../gameplay/port_scoregame.h"
#include "../gameplay/port_teams.h"      // kTeamBases
#include "../platform/port_settings.h"   // "keepReplays"
#include "glues.h"                       // sub_49E740, the score's sounds
#include "snd.h"                         // stop_digital
#include "replay.h"                      // copy_lastreplay_to
#include "../data/port_stormapi.h"       // SFileGetUserDataPath

using namespace FrontKit;

namespace {

// The design's inks that every theme keeps (score.css, lobby.css, bench.css).
const UiColor kWinInk = KitRgb(0xffe490);
const UiColor kLossInk = KitRgb(0xff8a65);
const UiColor kGoodInk = KitRgb(0x7bd88f);
const UiColor kMarkInk = KitRgb(0x66d5fe);
const UiColor kInkPlayer = KitRgb(0x64d0f9);
const UiColor kInkWhite = KitRgb(0xffffff);
const UiColor kTrack = {8 / 255.0f, 30 / 255.0f, 56 / 255.0f, 0.9f};
const UiColor kRowLine = {39 / 255.0f, 96 / 255.0f, 150 / 255.0f, 0.22f};
const UiColor kRowEven = {8 / 255.0f, 30 / 255.0f, 56 / 255.0f, 0.3f};
const UiColor kBandFill = {18 / 255.0f, 64 / 255.0f, 112 / 255.0f, 0.28f};
const UiColor kDim = {2 / 255.0f, 4 / 255.0f, 8 / 255.0f, 0.4f};

const char kRaceLetters[] = {'Z', 'T', 'P'};
const char *const kTabNames[SCORE_TABS] = {PT_("Overview"), PT_("Units"), PT_("Structures"), PT_("Resources")};
const char kTabKeys[SCORE_TABS] = {'V', 'U', 'T', 'R'};
// a tab's titles, and the short ones two columns use; 1.17 says "Total Score" on every tab,
// the design names the tab's own score
const char *const kTitles[SCORE_TABS][SCORE_CELLS] = {
    {PT_("Units"), PT_("Structures"), PT_("Resources"), PT_("Total Score")},
    {PT_("Produced"), PT_("Killed"), PT_("Lost"), PT_("Units Score")},
    {PT_("Constructed"), PT_("Razed"), PT_("Lost"), PT_("Structures Score")},
    {PT_("Gas Mined"), PT_("Minerals Mined"), PT_("Total Spent"), PT_("Resources Score")}};
const char *const kShort[SCORE_TABS][SCORE_CELLS] = {
    {PT_("Units"), PT_("Built"), PT_("Resources"), PT_("Total")},
    {PT_("Produced"), PT_("Killed"), PT_("Lost"), PT_("Score")},
    {PT_("Built"), PT_("Razed"), PT_("Lost"), PT_("Score")},
    {PT_("Gas"), PT_("Minerals"), PT_("Spent"), PT_("Score")}};
const char *const kDensities[] = {PT_("Auto"), PT_("Rows"), PT_("Compact"), PT_("Columns")};
const char *const kLists[] = {PT_("Players"), PT_("By Team"), PT_("Teams")};
enum { DENSITY_AUTO, DENSITY_ROWS, DENSITY_COMPACT, DENSITY_COLUMNS };
enum { LIST_PLAYERS, LIST_BYTEAM, LIST_TEAMS };
enum { LINE_PLAYER, LINE_TEAM, LINE_FOLD, LINE_SUB };

// A line of the list: a player, a team's band, a team's row in Teams, or a player under it.
struct Line {
  int nKind;
  int n;  // the player's or the team's index
};

// A list's grid (score.css): its side padding, the gap between its columns, the place
// number's column (0 none), the player's and the score's, all in SC2's units; the three
// cells share what is left.
struct Grid {
  float fPadL, fPadR, fGap, fPlace, fPlayer, fScore;
};
const Grid kGridRows = {16, 12, 24, 0, 470, 232};
const Grid kGridCompact = {16, 12, 20, 40, 400, 200};
const Grid kGridColumns = {12, 8, 12, 30, 262, 100};
const Grid kGridFold = {16, 12, 20, 40, 410, 232};

// A value counting up (1.17's scorerow_t lAnim, lTarget and lScale).
struct Count {
  long long nAnim = 0, nTarget = 0;
  int nScale = 1;
};

ScoreGame g_g;
bool g_bFromHome;            // opened from home (HYPERION_FRONT_OPEN=score-<preset>), not after a game
int g_nResult;
int g_nTab;
int g_nSortCol = -1;         // the column sorted by, 0 to 3, -1 1.17's order
int g_nSortDir = 1;          // 1 highest first, -1 lowest
std::vector<bool> g_mark;    // by player
std::vector<bool> g_open;    // by team, in Teams
bool g_bOpened;              // Teams has opened the local player's team
int g_nDensity = DENSITY_AUTO;
int g_nList = -1;            // -1 as the game has it
std::vector<Count> g_counts; // a player's four, then a team's four
bool g_bCounting;
float g_fTickMs;
float g_fScroll[2];          // pixels, a list each
bool g_bCentre;              // bring the local player into view
float g_fIn;                 // the panel's seconds since it came
bool g_bSaveDlg;
std::string g_saveName;
std::string g_saveNote;
bool g_bReplace;             // Save again replaces the replay of that name
bool g_bSaved;

std::string Clock(int nSeconds)
{
  if (nSeconds < 0)
    nSeconds = 0;
  char sz[32];
  if (nSeconds >= 3600)
    snprintf(sz, sizeof sz, "%d:%02d:%02d", nSeconds / 3600, nSeconds / 60 % 60, nSeconds % 60);
  else
    snprintf(sz, sizeof sz, "%02d:%02d", nSeconds / 60, nSeconds % 60);
  return sz;
}

UiColor Rgb(unsigned rgb, float a = 1.0f)
{
  return UiRgb(rgb, a);
}

// ---------------------------------------------------------------------------
// The game's numbers.

int Players(void)
{
  return static_cast<int>(g_g.players.size());
}

int Teams(void)
{
  return static_cast<int>(g_g.teams.size());
}

unsigned TeamRgb(int q)
{
  return kTeamBases[g_g.teams[q].nIndex & 15];
}

int Value(int i, int nTab, int k)
{
  return g_g.players[i].anValue[nTab][k];
}

// A team's number: its players' sum, or in 1.17's team games its captain's.
int TeamValue(int q, int nTab, int k)
{
  long long n = 0;
  for (const ScorePlayer &p : g_g.players)
    if (p.nTeam == q) {
      n += p.anValue[nTab][k];
      if (g_g.bShared)
        break;
    }
  return static_cast<int>(n);
}

// A team won with any of its players, lost with all of them, else plays on; it fell when the
// last of them went out.
int TeamResult(int q, int *pnOutAt)
{
  bool bAny = false, bAllLost = true;
  int nOut = -1;
  for (const ScorePlayer &p : g_g.players)
    if (p.nTeam == q) {
      bAny = true;
      if (p.nResult == SCORE_WON)
        return SCORE_WON;
      if (p.nResult != SCORE_LOST)
        bAllLost = false;
      nOut = p.nOutAt > nOut ? p.nOutAt : nOut;
    }
  if (pnOutAt != NULL)
    *pnOutAt = nOut;
  return bAny && bAllLost ? SCORE_LOST : SCORE_PLAYING;
}

int TeamSize(int q)
{
  int n = 0;
  for (const ScorePlayer &p : g_g.players)
    n += p.nTeam == q;
  return n;
}

int YourTeam(void)
{
  for (const ScorePlayer &p : g_g.players)
    if (p.bYou)
      return p.nTeam;
  return -1;
}

// The column's best, over the players or over the teams.
int Best(bool bTeams, int k)
{
  int n = 0;
  if (bTeams) {
    for (int q = 0; q < Teams(); ++q)
      n = (std::max)(n, TeamValue(q, g_nTab, k));
  } else {
    for (int i = 0; i < Players(); ++i)
      n = (std::max)(n, Value(i, g_nTab, k));
  }
  return n;
}

// The players in nTab's order: 1.17's (ScoreBefore), or by a column when one is sorted.
void Order(std::vector<int> *pWhich, int nTab, bool bSorted)
{
  std::stable_sort(pWhich->begin(), pWhich->end(), [&](int a, int b) {
    const ScorePlayer &pa = g_g.players[a], &pb = g_g.players[b];
    if (bSorted && g_nSortCol >= 0) {
      const long long d = static_cast<long long>(pa.anValue[nTab][g_nSortCol]) - pb.anValue[nTab][g_nSortCol];
      if (d != 0)
        return d * g_nSortDir > 0;
      return pa.nSlot < pb.nSlot;
    }
    return ScoreBefore(pa.nResult, pa.anValue[nTab][3], pa.nSlot, pb.nResult, pb.anValue[nTab][3], pb.nSlot);
  });
}

void OrderTeams(std::vector<int> *pWhich, bool bSorted)
{
  std::stable_sort(pWhich->begin(), pWhich->end(), [&](int a, int b) {
    if (bSorted && g_nSortCol >= 0) {
      const long long d = static_cast<long long>(TeamValue(a, g_nTab, g_nSortCol)) - TeamValue(b, g_nTab, g_nSortCol);
      if (d != 0)
        return d * g_nSortDir > 0;
      return a < b;
    }
    return ScoreBefore(TeamResult(a, NULL), TeamValue(a, g_nTab, 3), a, TeamResult(b, NULL), TeamValue(b, g_nTab, 3), b);
  });
}

// A player's place in 1.17's order of the tab, from 1.
int Rank(int i, int nTab)
{
  std::vector<int> all;
  for (int k = 0; k < Players(); ++k)
    all.push_back(k);
  Order(&all, nTab, false);
  return static_cast<int>(std::find(all.begin(), all.end(), i) - all.begin()) + 1;
}

int ListMode(void)
{
  if (Teams() == 0)
    return LIST_PLAYERS;
  if (g_nList >= 0)
    return g_nList;
  return Players() > 8 ? LIST_BYTEAM : LIST_PLAYERS;
}

int DensityNow(void)
{
  if (g_nDensity != DENSITY_AUTO)
    return g_nDensity;
  return Players() <= 8 ? DENSITY_ROWS : Players() <= 24 ? DENSITY_COMPACT : DENSITY_COLUMNS;
}

std::vector<Line> Lines(void)
{
  std::vector<Line> lines;
  const int nMode = ListMode();
  if (nMode == LIST_PLAYERS) {
    std::vector<int> all;
    for (int i = 0; i < Players(); ++i)
      all.push_back(i);
    Order(&all, g_nTab, true);
    for (int i : all)
      lines.push_back({LINE_PLAYER, i});
    return lines;
  }
  std::vector<int> teams;
  for (int q = 0; q < Teams(); ++q)
    teams.push_back(q);
  OrderTeams(&teams, nMode == LIST_TEAMS);
  for (int q : teams) {
    lines.push_back({nMode == LIST_TEAMS ? LINE_FOLD : LINE_TEAM, q});
    if (nMode == LIST_TEAMS && !(q < static_cast<int>(g_open.size()) && g_open[q]))
      continue;
    std::vector<int> mine;
    for (int i = 0; i < Players(); ++i)
      if (g_g.players[i].nTeam == q)
        mine.push_back(i);
    Order(&mine, g_nTab, nMode == LIST_BYTEAM);
    for (int i : mine)
      lines.push_back({nMode == LIST_TEAMS ? LINE_SUB : LINE_PLAYER, i});
  }
  // the players of no team, after the teams
  std::vector<int> rest;
  for (int i = 0; i < Players(); ++i)
    if (g_g.players[i].nTeam < 0)
      rest.push_back(i);
  Order(&rest, g_nTab, true);
  for (int i : rest)
    lines.push_back({nMode == LIST_TEAMS ? LINE_SUB : LINE_PLAYER, i});
  return lines;
}

// ---------------------------------------------------------------------------
// The count-up (gluScore_tab_ctrl_updatetab, sub_4629F0, sub_462F70).

Count &CountOf(bool bTeam, int n, int k)
{
  return g_counts[((bTeam ? Players() : 0) + n) * SCORE_CELLS + k];
}

long long ShownOf(const Count &c)
{
  return c.nAnim / c.nScale;
}

void CountEnd(void)
{
  if (!g_bCounting)
    return;
  g_bCounting = false;
  stop_digital();
  sub_49E740(0xA9);
}

// The tab's numbers from 0, as 1.17 starts them on each tab, or at once at their end.
void CountStart(bool bInstant)
{
  g_counts.assign(static_cast<size_t>(Players() + Teams()) * SCORE_CELLS, Count());
  for (int i = 0; i < Players() + Teams(); ++i)
    for (int k = 0; k < SCORE_CELLS; ++k) {
      const bool bTeam = i >= Players();
      const int v = bTeam ? TeamValue(i - Players(), g_nTab, k) : Value(i, g_nTab, k);
      Count &c = CountOf(bTeam, bTeam ? i - Players() : i, k);
      c.nScale = ScoreScaleOf(v);
      c.nTarget = static_cast<long long>(v) * c.nScale;
      c.nAnim = bInstant ? c.nTarget : 0;
    }
  stop_digital();
  g_bCounting = !bInstant;
  g_fTickMs = 0.0f;
  // 1.17 plays the counting while any column has a number to count
  if (g_bCounting && Best(false, 0) + Best(false, 1) + Best(false, 2) > 0)
    sub_49E740(0xA8);
}

void CountFinish(void)
{
  for (Count &c : g_counts)
    c.nAnim = c.nTarget;
  CountEnd();
}

// 1.17's timer runs every 20 ms; a frame counts at most 200 ms of it.
void CountFrame(void)
{
  static Uint64 s_qLast;
  const Uint64 qNow = SDL_GetTicks();
  const float fDt = s_qLast != 0 ? static_cast<float>(qNow - s_qLast) : 0.0f;
  s_qLast = qNow;
  if (!g_bCounting)
    return;
  g_fTickMs += (std::min)(200.0f, fDt);
  while (g_fTickMs >= SCORE_TICK_MS) {
    g_fTickMs -= SCORE_TICK_MS;
    bool bMoving = false;
    for (Count &c : g_counts)
      if (c.nAnim != c.nTarget) {
        c.nAnim = ScoreStep(c.nAnim, c.nTarget);
        bMoving = true;
      }
    if (!bMoving) {
      CountEnd();
      break;
    }
  }
}

void SetTab(int nTab)
{
  if (nTab == g_nTab)
    return;
  g_nTab = nTab;
  g_nSortCol = -1;
  CountStart(false);
}

// ---------------------------------------------------------------------------
// Saving the replay (1.17's Save Replay, gluScore_flc_saveReplay_ctrl_fcn).

bool KeepsReplays(void)
{
  double d = 1.0;
  return !(PortSettingNumber("hyperion", "keepReplays", &d) && d == 0.0);
}

bool CanSave(void)
{
  return g_g.end.bSaveable && !g_bSaved && !g_bFromHome && !KeepsReplays();
}

const char *SaveLabel(void)
{
  return g_bSaved || (g_g.end.bSaveable && !g_bFromHome && KeepsReplays()) ? PortText("Replay Saved")
                                                                           : PortText("Save Replay");
}

bool NameAllowed(const std::string &s)
{
  return !s.empty() && s.find_first_of("\\/:*?\"<>|") == std::string::npos;
}

void SaveOpen(void)
{
  if (!CanSave())
    return;
  // the map's name with only its letters and digits, and the clock
  std::string name;
  for (char c : g_g.map)
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'))
      name += c;
  std::string clock = Clock(g_g.end.nSeconds);
  std::replace(clock.begin(), clock.end(), ':', '.');
  g_saveName = (name.empty() ? std::string("Replay") : name) + " " + clock;
  g_saveNote.clear();
  g_bReplace = false;
  g_bSaveDlg = true;
}

void Save(void)
{
  if (!NameAllowed(g_saveName)) {
    g_saveNote = PortText("A replay's name cannot be empty or hold \\ / : * ? \" < > |.");
    return;
  }
  char szDir[MAX_PATH] = "";
  SFileGetUserDataPath(szDir, sizeof szDir, 0);
  const std::string path = std::string(szDir) + "\\maps\\replays\\" + g_saveName + ".rep";
  if (!g_bReplace && GetFileAttributesA(path.c_str()) != INVALID_FILE_ATTRIBUTES) {
    g_saveNote = PortText("A replay of that name is there already. Save again to replace it.");
    g_bReplace = true;
    return;
  }
  if (!copy_lastreplay_to(g_saveName.c_str())) {
    g_saveNote = PortText("The replay could not be saved.");
    return;
  }
  FrontLog("score: the replay saved as %s\n", g_saveName.c_str());
  g_bSaved = true;
  g_bSaveDlg = false;
}

// ---------------------------------------------------------------------------
// Drawing.

// A line's ground (score.css .sco-row): n bit 1 the rows' inset look (else compact's rule),
// 2 under the mouse, 4 an even compact row, 8 the local player's, 16 marked, 32 a team's band,
// 64 tight; look.top is the colour edge's, f the gap below it in pixels.
void DrawLine(float x, float y, float w, float h, void *pCtx)
{
  const ScDraw &d = *static_cast<const ScDraw *>(pCtx);
  const bool bRows = (d.n & 1) != 0, bHot = (d.n & 2) != 0, bEven = (d.n & 4) != 0, bYou = (d.n & 8) != 0,
             bMark = (d.n & 16) != 0, bBand = (d.n & 32) != 0, bTight = (d.n & 64) != 0;
  h -= d.f;
  const float r = bRows ? Sc(4) : 0.0f;
  if (bRows) {
    UiFill(x, y, w, h, bHot ? kLbItemHi : kLbItem, r);
    UiRing(x, y, w, h, kLbItemRim, r, fmaxf(1.0f, Sc(1)));
  } else {
    if (bBand)
      UiFill(x, y, w, h, kBandFill);
    else if (bHot)
      UiFill(x, y, w, h, kLbItemHi);
    else if (bEven)
      UiFill(x, y, w, h, kRowEven);
    UiFill(x, y + h - fmaxf(1.0f, Sc(1)), w, fmaxf(1.0f, Sc(1)), kRowLine);
  }
  const float fInset = bTight ? Sc(2) : Sc(4);
  UiFill(x, y + fInset, Sc(4), h - 2 * fInset, d.look.top, Sc(2));
  if (bMark && bYou) {
    UiRing(x, y, w, h, kMarkInk, r, Sc(4));
    UiRing(x, y, w, h, kLbHyperionRim, r, Sc(2));
  } else if (bYou) {
    UiRing(x, y, w, h, kLbHyperionRim, r, Sc(2));
  } else if (bMark) {
    UiRing(x, y, w, h, kMarkInk, r, Sc(2));
  }
}

// The race's box (.sco-race): the player's colour round a dark fill of it.
void DrawRace(float x, float y, float w, float h, void *pCtx)
{
  const ScDraw &d = *static_cast<const ScDraw *>(pCtx);
  const UiColor c = d.look.top;
  const UiColor fill = {c.r * 0.28f + 1 / 255.0f * 0.72f, c.g * 0.28f + 9 / 255.0f * 0.72f,
                        c.b * 0.28f + 24 / 255.0f * 0.72f, 0.93f};
  const float fRim = d.n != 0 ? fmaxf(1.0f, Sc(1)) : Sc(2);
  UiFill(x, y, w, h, fill, Sc(3));
  UiRing(x, y, w, h, c, Sc(3), fRim);
}

// A bar (.sco-bar): its track, and its fill in the player's colour, f of the way.
void DrawBar(float x, float y, float w, float h, void *pCtx)
{
  const ScDraw &d = *static_cast<const ScDraw *>(pCtx);
  const float r = Sc(2);
  UiFill(x, y, w, h, kTrack, r);
  if (d.f <= 0.0f)
    return;
  const float fw = fmaxf(w * d.f, fmaxf(1.0f, w * 0.006f));
  UiFill(x, y, fw, h, d.look.top, r);
  UiFill(x, y, fw, fminf(h, Sc(4)), {1.0f, 1.0f, 1.0f, 0.12f}, r);
  UiRing(x, y, fw, h, {1.0f, 1.0f, 1.0f, 0.16f}, r, fmaxf(1.0f, Sc(1)));
}

// The score's box in the rows (.sco-score).
void DrawScoreBox(float x, float y, float w, float h, void *)
{
  UiFill(x, y, w, h, kTrack, Sc(3));
  UiRing(x, y, w, h, kLbInnerRim, Sc(3), fmaxf(1.0f, Sc(1)));
}

// The sort's arrow beside a title, and a team's fold: a chevron, n 1 down, 2 up, 3 right.
void DrawChevron(float x, float y, float w, float h, void *pCtx)
{
  const ScDraw &d = *static_cast<const ScDraw *>(pCtx);
  const float cx = x + w / 2, cy = y + h / 2, s = fminf(w, h) * 0.3f, t = fmaxf(1.5f, Sc(2));
  if (d.n == 3) {
    UiLine(cx - s * 0.5f, cy - s, cx + s * 0.5f, cy, d.look.top, t);
    UiLine(cx + s * 0.5f, cy, cx - s * 0.5f, cy + s, d.look.top, t);
  } else {
    const float k = d.n == 2 ? -1.0f : 1.0f;
    UiLine(cx - s, cy - k * s * 0.5f, cx, cy + k * s * 0.5f, d.look.top, t);
    UiLine(cx, cy + k * s * 0.5f, cx + s, cy - k * s * 0.5f, d.look.top, t);
  }
}

ScLook Look(UiColor c)
{
  return ScLook{c, c, c, c, c, 0};
}

void Chevron(float fSide, int n, UiColor c)
{
  Decl a;
  a.d.layout.sizing.width = Fixed(Sc(fSide));
  a.d.layout.sizing.height = Fixed(Sc(fSide));
  a.d.custom.customData = Custom(DrawChevron, Look(c), 0.0f, n);
  Open(a);
  Close();
}

const char *ResultWord(const ScorePlayer &p, std::string *pOut)
{
  // {0} the time on the game's clock
  if (p.bYou && g_g.bLeft)
    *pOut = PortTextF("Left at {0}", {Clock(g_g.nLeftAt)});
  else if (p.nResult == SCORE_WON)
    *pOut = PortText("Victory");
  else if (p.nResult == SCORE_DRAW)
    *pOut = PortText("Draw");
  else if (p.nResult == SCORE_DROPPED)
    *pOut = p.nOutAt >= 0 ? PortTextF("Dropped at {0}", {Clock(p.nOutAt)}) : std::string(PortText("Disconnected"));
  else if (p.nResult == SCORE_LOST)
    *pOut = p.nOutAt >= 0 && !p.bYou ? PortTextF("Out at {0}", {Clock(p.nOutAt)}) : std::string(PortText("Defeat"));
  else
    *pOut = PortText("Still playing");
  return pOut->c_str();
}

std::string TeamWord(int q)
{
  int nOut = -1;
  const int r = TeamResult(q, &nOut);
  return r == SCORE_WON ? std::string(PortText("Victory"))
         : r == SCORE_LOST
             ? (nOut >= 0 ? PortTextF("Fell at {0}", {Clock(nOut)}) : std::string(PortText("Defeat")))
             : std::string(PortText("Still playing"));
}

// The race's box, with its letter, or a team's count of players.
void RaceBox(float fSide, unsigned rgb, const std::string &text, bool bMono, bool bTight)
{
  Decl b;
  b.d.layout.sizing.width = Fixed(Sc(fSide));
  b.d.layout.sizing.height = Fixed(Sc(fSide));
  b.d.layout.childAlignment.x = CLAY_ALIGN_X_CENTER;
  b.d.layout.childAlignment.y = CLAY_ALIGN_Y_CENTER;
  b.d.custom.customData = Custom(DrawRace, Look(Rgb(rgb)), 0.0f, bTight ? 1 : 0);
  Open(b);
  const float fText = fSide >= 40 ? (bMono ? 18 : 20) : fSide >= 24 ? 12 : 10;
  Text(Keep(text), bMono ? UI_FACE_MONO : UI_FACE_WIDE, Fs(fText), 0.0f, kInkWhite, -1, 0.0f, true);
  Close();
}

// A number and its bar: over it in the rows, beside it (the number at the right) otherwise.
void Cell(long long nShown, int nBest, unsigned rgb, bool bRows, float fNumW, float fNumSize, UiColor ink)
{
  Decl c;
  c.d.layout.sizing.width = Grow();
  c.d.layout.childAlignment.y = CLAY_ALIGN_Y_CENTER;
  if (bRows) {
    c.d.layout.layoutDirection = CLAY_TOP_TO_BOTTOM;
    c.d.layout.childGap = Gap(Fs(5));
  } else {
    c.d.layout.childGap = Gap(Fs(10));
  }
  Open(c);
  const std::string num = Thousands(nShown);
  if (bRows)
    Text(Keep(num), UI_FACE_MONO, Fs(fNumSize), 0.0f, ink);
  Decl bar;
  bar.d.layout.sizing.width = Grow();
  bar.d.layout.sizing.height = Fixed(Sc(bRows ? 12 : 8));
  const float f = nBest > 0 ? static_cast<float>(nShown) / nBest : 0.0f;
  bar.d.custom.customData = Custom(DrawBar, Look(Rgb(rgb)), f > 1.0f ? 1.0f : f);
  Open(bar);
  Close();
  if (!bRows) {
    Decl n;
    n.d.layout.sizing.width = Fixed(Sc(fNumW));
    n.d.layout.childAlignment.x = CLAY_ALIGN_X_RIGHT;
    Open(n);
    Text(Keep(num), UI_FACE_MONO, Fs(fNumSize), 0.0f, ink);
    Close();
  }
  Close();
}

void Fixed1(float fW, Clay_ChildAlignment align = {CLAY_ALIGN_X_LEFT, CLAY_ALIGN_Y_CENTER})
{
  Decl b;
  b.d.layout.sizing.width = Fixed(Sc(fW));
  b.d.layout.childAlignment = align;
  Open(b);
}

// One line of a list, nKind's look in the list's density: its ground, the place number, the
// race (or the team's count), the name and what became of them, the three cells and the score.
void LineRow(const Line &l, int nDensity, int nIndex, float fSlot, float fGap, bool bTight, int nPlace)
{
  const bool bTeam = l.nKind == LINE_TEAM || l.nKind == LINE_FOLD;
  const bool bSub = l.nKind == LINE_SUB;
  const bool bRows = !bSub && (nDensity == DENSITY_ROWS || l.nKind == LINE_FOLD);
  const bool bCols = nDensity == DENSITY_COLUMNS && !bSub && l.nKind != LINE_FOLD;
  const Grid &grid = l.nKind == LINE_FOLD ? kGridFold : bSub ? kGridCompact : bRows ? kGridRows : bCols ? kGridColumns
                                                                                                      : kGridCompact;
  const ScorePlayer *p = bTeam ? NULL : &g_g.players[l.n];
  const unsigned rgb = bTeam ? TeamRgb(l.n) : p->rgb;
  const bool bYou = !bTeam && p->bYou;
  const bool bYourTeam = bTeam && l.n == YourTeam();
  const bool bMark = !bTeam && l.n < static_cast<int>(g_mark.size()) && g_mark[l.n];
  const int nResult = bTeam ? TeamResult(l.n, NULL) : p->nResult;
  const bool bWon = nResult == SCORE_WON;

  Decl row = Box(bTeam ? (l.nKind == LINE_FOLD ? "scfold" : "scband") : bSub ? "scsub" : "scrow",
                 bTeam ? l.n : p->nSlot);
  const bool bHot = Over(row.d.id);
  if (Clicked(row.d.id)) {
    if (l.nKind == LINE_FOLD) {
      if (static_cast<int>(g_open.size()) < Teams())
        g_open.resize(Teams(), false);
      g_open[l.n] = !g_open[l.n];
    } else if (!bTeam) {
      if (static_cast<int>(g_mark.size()) < Players())
        g_mark.resize(Players(), false);
      g_mark[l.n] = !g_mark[l.n];
    }
  }
  row.d.layout.sizing.width = Grow();
  row.d.layout.sizing.height = Fixed(fSlot);
  row.d.layout.padding = Pad(Fs(grid.fPadL + (bSub ? 28 : 0)), Fs(grid.fPadR), 0, fGap / UiScale());
  row.d.layout.childGap = Gap(Fs(grid.fGap));
  row.d.layout.childAlignment.y = CLAY_ALIGN_Y_CENTER;
  const int n = (bRows ? 1 : 0) | (bHot && !bTeam ? 2 : 0) | (nIndex % 2 == 1 ? 4 : 0) | (bYou ? 8 : 0) |
                (bMark ? 16 : 0) | (l.nKind == LINE_TEAM ? 32 : 0) | (bTight ? 64 : 0);
  row.d.custom.customData = Custom(DrawLine, Look(Rgb(rgb)), fGap, n);
  Open(row);

  // the place, or a team's fold
  if (grid.fPlace > 0) {
    Fixed1(grid.fPlace, {CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER});
    if (l.nKind == LINE_FOLD)
      Chevron(18, l.n < static_cast<int>(g_open.size()) && g_open[l.n] ? 1 : 3, kScInkHead);
    else if (nPlace > 0)
      Text(Keep(std::to_string(nPlace)), UI_FACE_MONO, Fs(bCols ? 13 : 14), 0.0f, bWon ? kWinInk : kLbInkDesc);
    Close();
  }

  // the player: the race's box, the name and the line under it
  Fixed1(grid.fPlayer);
  const float fRace = bRows ? 48 : bTight ? 18 : 26;
  const std::string race = bTeam ? std::to_string(TeamSize(l.n)) : std::string(1, kRaceLetters[p->nRace]);
  RaceBox(fRace, rgb, race, bTeam, bTight && !bRows);
  Decl gap;
  gap.d.layout.sizing.width = Fixed(Sc(bRows ? 12 : 10));
  Open(gap);
  Close();
  Decl who;
  who.d.layout.sizing.width = Grow();
  who.d.layout.layoutDirection = bRows ? CLAY_TOP_TO_BOTTOM : CLAY_LEFT_TO_RIGHT;
  who.d.layout.childAlignment.y = bRows ? CLAY_ALIGN_Y_CENTER : CLAY_ALIGN_Y_BOTTOM;
  who.d.layout.childGap = Gap(Fs(bRows ? 1 : 8));
  Open(who);
  const float fWho = grid.fPlayer - fRace - (bRows ? 12 : 10);
  std::string name, under;
  if (bTeam) {
    name = Upper(g_g.teams[l.n].name.c_str());
    under = TeamWord(l.n);
  } else {
    name = p->name.empty() ? std::string(PortText("Computer")) : p->name;
    std::string word;
    ResultWord(*p, &word);
    if (bCols) {
      under = word;
    } else {
      if (p->nTeam >= 0 && ListMode() == LIST_PLAYERS)
        under = g_g.teams[p->nTeam].name + kDot;
      if (p->bComputer)
        // {0} the computer's level
        under += (p->nLevel > 0 ? PortTextF("Computer, {0}", {PortText(AiLevelName(p->nLevel))})
                                : std::string(PortText("Computer"))) +
                 kDot;
      under += word;
    }
  }
  const float fNameSize = bTeam ? (bRows ? 17 : 14) : bRows ? 21 : bCols ? 15 : 17;
  // a player under a team's band in a lighter weight (score.css .sco-in-team)
  const UiFace face = bTeam ? UI_FACE_WIDE : (bSub || ListMode() == LIST_BYTEAM) ? UI_FACE_TEXT : UI_FACE_BOLD;
  const UiColor nameInk = bTeam ? (bYourTeam ? kWinInk : kScInkHead) : p->bYou ? kWinInk : kInkPlayer;
  const float fNameW = bRows ? fWho : fWho * (bCols ? 0.66f : 0.6f);
  const std::string shownName = Elide(name, face, Fs(fNameSize), Sc(fNameW));
  Text(Keep(shownName), face, Fs(fNameSize), bTeam ? 0.04f : 0.0f, nameInk);
  const float fUnderSize = bRows ? 14 : 13;
  const float fUnderW = bRows ? fWho : fWho - UiTextWidth(face, roundf(UiPx(Fs(fNameSize))), 0.0f, shownName.c_str()) / Sc(1) - 8;
  if (fUnderW > 20)
    Text(Keep(Elide(under, UI_FACE_TEXT, Fs(fUnderSize), Sc(fUnderW))), UI_FACE_TEXT, Fs(fUnderSize), 0.0f,
         bWon ? kGoodInk : kLbInkDesc);
  Close();
  Close();

  // the three cells and the score
  const float fNumW = bCols ? 58 : 86, fNumSize = bRows ? 19 : bCols ? 14 : 16;
  const UiColor numInk = bTeam && !bRows ? kScInkHead : kLbInkInfo;
  for (int k = 0; k < SCORE_COLUMNS; ++k)
    Cell(ShownOf(CountOf(bTeam, l.n, k)), Best(bTeam, k), rgb, bRows, fNumW, fNumSize, numInk);
  Decl score;
  score.d.layout.sizing.width = Fixed(Sc(grid.fScore));
  score.d.layout.childAlignment.x = CLAY_ALIGN_X_RIGHT;
  score.d.layout.childAlignment.y = CLAY_ALIGN_Y_CENTER;
  Open(score);
  const std::string total = Thousands(ShownOf(CountOf(bTeam, l.n, 3)));
  if (bRows) {
    Decl box;
    box.d.layout.sizing.width = Grow();
    box.d.layout.sizing.height = Fixed(fSlot - fGap - Sc(16));
    box.d.layout.padding = Pad(Fs(16), Fs(16), 0, 0);
    box.d.layout.childAlignment.x = CLAY_ALIGN_X_RIGHT;
    box.d.layout.childAlignment.y = CLAY_ALIGN_Y_CENTER;
    box.d.custom.customData = Custom(DrawScoreBox, kScTab);
    Open(box);
    Text(Keep(total), UI_FACE_MONO, Fs(26), 0.0f, bWon ? kWinInk : kInkWhite);
    Close();
  } else {
    Text(Keep(total), UI_FACE_MONO, Fs(bCols ? 15 : 18), 0.0f, bWon ? kWinInk : kInkWhite);
  }
  Close();
  Close();
}

// The titles over a list, each sorting by its column (Player, 1.17's order again).
void Titles(int nList, int nDensity)
{
  const bool bCols = nDensity == DENSITY_COLUMNS;
  const Grid &grid = ListMode() == LIST_TEAMS ? kGridFold : nDensity == DENSITY_ROWS ? kGridRows : bCols ? kGridColumns
                                                                                                        : kGridCompact;
  Decl head;
  head.d.layout.sizing.width = Grow();
  head.d.layout.sizing.height = Fixed(Sc(40));
  head.d.layout.padding = Pad(Fs(grid.fPadL), Fs(grid.fPadR), 0, 0);
  head.d.layout.childGap = Gap(Fs(grid.fGap));
  head.d.layout.childAlignment.y = CLAY_ALIGN_Y_CENTER;
  head.d.border = Border(kLbDivider, 0, 0, 0, Fs(2));
  Open(head);
  const float fSize = bCols ? 12 : 14, fSpacing = bCols ? 0.02f : 0.04f;
  const auto Title = [&](int k, const char *psz, float fW, bool bRight) {
    Decl t = Box("sctitle", nList * 8 + k + 1);
    if (fW > 0)
      t.d.layout.sizing.width = Fixed(Sc(fW));
    else
      t.d.layout.sizing.width = Grow();
    t.d.layout.childGap = Gap(Fs(6));
    t.d.layout.childAlignment.x = bRight ? CLAY_ALIGN_X_RIGHT : CLAY_ALIGN_X_LEFT;
    t.d.layout.childAlignment.y = CLAY_ALIGN_Y_CENTER;
    const bool bOn = k >= 0 && k == g_nSortCol;
    const bool bHot = Over(t.d.id);
    if (Clicked(t.d.id)) {
      if (k < 0) {
        g_nSortCol = -1;
      } else if (k == g_nSortCol) {
        g_nSortDir = -g_nSortDir;
      } else {
        g_nSortCol = k;
        g_nSortDir = 1;
      }
    }
    Open(t);
    const UiColor c = bOn ? kWinInk : bHot ? kInkWhite : kScInkHead;
    Text(Keep(Upper(psz)), UI_FACE_WIDE, Fs(fSize), fSpacing, c);
    if (bOn)
      Chevron(12, g_nSortDir > 0 ? 1 : 2, c);
    Close();
  };
  if (grid.fPlace > 0) {
    Fixed1(grid.fPlace);
    Close();
  }
  Title(-1, ListMode() == LIST_TEAMS ? PortText("Team") : PortText("Player"), grid.fPlayer, false);
  for (int k = 0; k < SCORE_COLUMNS; ++k)
    Title(k, PortText(bCols ? kShort[g_nTab][k] : kTitles[g_nTab][k]), 0, false);
  Title(3, PortText(bCols ? kShort[g_nTab][3] : kTitles[g_nTab][3]), grid.fScore, true);
  Close();
}

// A list of lines, nList 0 or the second column's 1, fH pixels high for its lines; a flat
// list's places count on from nBase.
void List(int nList, const std::vector<Line> &lines, int nDensity, float fH, int nBase)
{
  Decl list;
  list.d.layout.layoutDirection = CLAY_TOP_TO_BOTTOM;
  list.d.layout.sizing.width = Grow();
  list.d.layout.sizing.height = Grow();
  Open(list);
  Titles(nList, nDensity);
  Decl space;
  space.d.layout.sizing.height = Fixed(Sc(8));
  Open(space);
  Close();

  // each line's slot: the rows' pitch with 6 below each, a compact line's with none
  const int nMode = ListMode();
  const float fH6 = fH / Sc(1);
  int nRowLines = 0, nSubLines = 0;
  for (const Line &l : lines)
    (l.nKind == LINE_SUB ? nSubLines : nRowLines)++;
  float fPitch;
  if (nMode == LIST_TEAMS)
    fPitch = (std::max)(64.0f, (std::min)(90.0f, floorf((fH6 - nSubLines * 38.0f) / (std::max)(1, nRowLines))));
  else if (nDensity == DENSITY_ROWS)
    fPitch = (std::max)(64.0f, (std::min)(90.0f, floorf(fH6 / (std::max)(1, nRowLines))));
  else
    fPitch = (std::max)(24.0f, (std::min)(nDensity == DENSITY_COMPACT ? 44.0f : 42.0f, floorf(fH6 / (std::max)(1, nRowLines))));
  const bool bTight = nDensity != DENSITY_ROWS && fPitch < 32;
  float fTotal = 0.0f;
  for (const Line &l : lines)
    fTotal += Sc(l.nKind == LINE_SUB ? 38.0f : fPitch);

  // the wheel scrolls a list that does not fit, which opens on the local player
  Decl body = Box("sclist", nList);
  body.d.layout.layoutDirection = CLAY_TOP_TO_BOTTOM;
  body.d.layout.sizing.width = Grow();
  body.d.layout.sizing.height = Grow();
  const float fMax = (std::max)(0.0f, fTotal - fH);
  if (fMax > 0) {
    if (g_bCentre) {
      float y = 0.0f;
      for (const Line &l : lines) {
        const float s = Sc(l.nKind == LINE_SUB ? 38.0f : fPitch);
        if ((l.nKind == LINE_PLAYER || l.nKind == LINE_SUB) && g_g.players[l.n].bYou)
          g_fScroll[nList] = y + s / 2 - fH / 2;
        y += s;
      }
    }
    g_fScroll[nList] += WheelPixels(Over(body.d.id), Sc(fPitch) * 3);
  }
  g_fScroll[nList] = g_fScroll[nList] < 0 ? 0 : g_fScroll[nList] > fMax ? fMax : g_fScroll[nList];
  body.d.clip.vertical = true;
  body.d.clip.childOffset.y = -g_fScroll[nList];
  Open(body);
  int nShown = 0;
  for (const Line &l : lines) {
    const bool bSub = l.nKind == LINE_SUB;
    const bool bRowsLook = !bSub && (nDensity == DENSITY_ROWS || l.nKind == LINE_FOLD);
    // the place: in a flat list its line's, under the teams the player's among all
    int nPlace = 0;
    if (l.nKind == LINE_PLAYER || bSub)
      nPlace = nMode == LIST_PLAYERS ? nBase + nShown + 1 : Rank(l.n, g_nTab);
    LineRow(l, nDensity, nShown, Sc(bSub ? 38.0f : fPitch), bRowsLook ? Sc(6) : 0.0f, bTight, nPlace);
    ++nShown;
  }
  Close();
  Close();
}

// The head (score.css .sco-head): the outcome, the game, and where the local player came.
void Head(float fW)
{
  Decl head = At("schead", 40, 16, fW - 40 - 760, 0);
  head.d.layout.layoutDirection = CLAY_TOP_TO_BOTTOM;
  Open(head);
  const ScorePlayer *you = NULL;
  for (const ScorePlayer &p : g_g.players)
    if (p.bYou)
      you = &p;
  const int nLocal = g_g.end.nLocal;
  const bool bWin = !g_g.end.bReplay && nLocal == 1;
  const char *pszOutcome = g_g.end.bReplay  ? PortText("Replay")
                           : nLocal == 1    ? PortText("Victory")
                           : nLocal == 3    ? PortText("Draw")
                           : nLocal == 4    ? PortText("Disconnected")
                                            : PortText("Defeat");
  Text(Keep(Upper(pszOutcome)), UI_FACE_WIDE, Fs(40), 0.06f, g_g.end.bReplay ? kScInkHead : bWin ? kWinInk : kLossInk,
       -1, 46.0f / 40.0f);
  // the game: map, type, players, and which game by which rules
  std::string game = g_g.map.empty() ? std::string(PortText("A game")) : g_g.map;
  const std::string type = !g_g.type.empty() ? g_g.type : GameTypeName(g_g.nGameType);
  // {0} which game it was, {1} the rules it was played by
  game += kDot + type + kDot + PortTextNF("{0} player", "{0} players", Players(), {std::to_string(Players())}) + kDot +
          PortTextF("{0}, {1} rules", {g_g.bExpansion ? "Brood War" : "StarCraft",
                                       g_g.bHyperion ? "Hyperion" : PortText("Classic")});
  Text(Keep(game), UI_FACE_TEXT, Fs(17), 0.0f, kLbInkInfo);
  if (you != NULL) {
    std::string line;
    const int i = static_cast<int>(you - &g_g.players[0]);
    if (g_g.bLeft) {
      line = std::string(kDot) + PortTextF("left at {0}, which counts as a loss", {Clock(g_g.nLeftAt)});
    } else if (you->nTeam >= 0) {
      std::vector<int> teams;
      for (int q = 0; q < Teams(); ++q)
        teams.push_back(q);
      // the teams' places by 1.17's order of the overview
      const int nTab = g_nTab;
      g_nTab = SCORE_OVERVIEW;
      OrderTeams(&teams, false);
      g_nTab = nTab;
      const int nPlace = static_cast<int>(std::find(teams.begin(), teams.end(), you->nTeam) - teams.begin()) + 1;
      line = std::string(kDot);
      if (TeamResult(you->nTeam, NULL) == SCORE_WON)
        // {0} the team's name
        line += PortTextF("{0}, the winning team", {g_g.teams[you->nTeam].name});
      else
        // {0} the team's place, {1} how many teams played
        line += g_g.teams[you->nTeam].name + kDot +
                PortTextF("{0} of {1} teams", {std::to_string(nPlace), std::to_string(Teams())});
      line += kDot + PortTextF("{0} of {1} by score",
                               {std::to_string(Rank(i, SCORE_OVERVIEW)), std::to_string(Players())});
    } else {
      // {0} the player's place, {1} how many played
      const std::string place =
          PortTextF("{0} of {1}", {std::to_string(Rank(i, SCORE_OVERVIEW)), std::to_string(Players())});
      line = std::string(kDot) + (you->nResult == SCORE_LOST && you->nOutAt >= 0
                                      ? PortTextF("{0}, out at {1}", {place, Clock(you->nOutAt)})
                                      : place);
    }
    Decl mine;
    mine.d.layout.padding = Pad(0, 0, Fs(2), 0);
    Open(mine);
    Text(Keep(you->name), UI_FACE_BOLD, Fs(16), 0.0f, kWinInk);
    Text(Keep(line), UI_FACE_TEXT, Fs(16), 0.0f, kLbInkDesc);
    Close();
  }
  Close();
}

void Tabs(void)
{
  Decl tabs = Floating("sctabs", 0, -Sc(34), Sc(34), 22, CLAY_ATTACH_POINT_RIGHT_TOP, CLAY_ATTACH_POINT_RIGHT_TOP);
  tabs.d.layout.childGap = Gap(Fs(8));
  Open(tabs);
  for (int t = 0; t < SCORE_TABS; ++t)
    if (ScMiniTab(Box("sctab", t), PortText(kTabNames[t]), kTabKeys[t], t == g_nTab))
      SetTab(t);
  Close();
}

// The table (.sco-box): one list, or two side by side past 24 players, a team's band kept
// whole in one of them.
void Table(float fW, float fBoxH)
{
  Decl box = At("scbox", 13, 112, fW - 34, fBoxH);
  box.d.layout.padding = Pad(Fs(16), Fs(16), Fs(14), Fs(14));
  box.d.layout.childGap = Gap(Fs(28));
  InnerBox(box);
  Open(box);
  const std::vector<Line> lines = Lines();
  const int nDensity = ListMode() == LIST_TEAMS ? DENSITY_ROWS : DensityNow();
  // the lines' room: the box less its padding, its rims, the titles and the space under them
  const float fH = Sc(fBoxH - 28 - 4 - 40 - 8);
  if (nDensity != DENSITY_COLUMNS) {
    List(0, lines, nDensity, fH, 0);
  } else {
    std::vector<Line> a, b;
    if (ListMode() == LIST_BYTEAM) {
      const size_t nTotal = lines.size();
      size_t nAt = 0;
      for (size_t k = 0; k < lines.size();) {
        size_t e = k + 1;
        while (e < lines.size() && lines[e].nKind != LINE_TEAM)
          ++e;
        const size_t n = e - k;
        std::vector<Line> &to = a.empty() || (b.empty() && nAt + (n + 1) / 2 <= nTotal / 2) ? a : b;
        to.insert(to.end(), lines.begin() + k, lines.begin() + e);
        nAt += n;
        k = e;
      }
    } else {
      const size_t nHalf = (lines.size() + 1) / 2;
      a.assign(lines.begin(), lines.begin() + nHalf);
      b.assign(lines.begin() + nHalf, lines.end());
    }
    List(0, a, nDensity, fH, 0);
    List(1, b, nDensity, fH, static_cast<int>(a.size()));
  }
  Close();
}

void FootLabel(const char *psz)
{
  Text(psz, UI_FACE_TEXT, Fs(14), 0.0f, kLbInkDesc);
}

// The foot (.sco-foot): the elapsed time, the list's switches, Save Replay and OK.
void Foot(float fW, float fH)
{
  Decl foot = At("scfoot", 40, fH - 33 - 52, fW - 80, 52);
  foot.d.layout.childGap = Gap(Fs(24));
  foot.d.layout.childAlignment.y = CLAY_ALIGN_Y_CENTER;
  Open(foot);
  Decl time;
  time.d.layout.childGap = Gap(Fs(14));
  time.d.layout.childAlignment.y = CLAY_ALIGN_Y_CENTER;
  Open(time);
  Text(PortText("ELAPSED TIME"), UI_FACE_WIDE, Fs(14), 0.04f, kScInkHead);
  Text(Keep(Clock(g_g.end.nSeconds)), UI_FACE_MONO, Fs(26), 0.0f, kInkWhite);
  Close();
  Decl mid;
  mid.d.layout.sizing.width = Grow();
  mid.d.layout.childGap = Gap(Fs(22));
  mid.d.layout.childAlignment.x = CLAY_ALIGN_X_CENTER;
  mid.d.layout.childAlignment.y = CLAY_ALIGN_Y_CENTER;
  Open(mid);
  Decl dens;
  dens.d.layout.childGap = Gap(Fs(10));
  dens.d.layout.childAlignment.y = CLAY_ALIGN_Y_CENTER;
  Open(dens);
  FootLabel(PortText("Density"));
  const char *const apszDensities[] = {PortText(kDensities[0]), PortText(kDensities[1]), PortText(kDensities[2]),
                                       PortText(kDensities[3])};
  const int nDensity = Segments("scdens", apszDensities, 4, g_nDensity);
  if (nDensity >= 0)
    g_nDensity = nDensity;
  Close();
  if (Teams() > 0) {
    Decl list;
    list.d.layout.childGap = Gap(Fs(10));
    list.d.layout.childAlignment.y = CLAY_ALIGN_Y_CENTER;
    Open(list);
    FootLabel(PortText("List"));
    const char *const apszLists[] = {PortText(kLists[0]), PortText(kLists[1]), PortText(kLists[2])};
    const int nList = Segments("sclistby", apszLists, 3, ListMode());
    if (nList >= 0) {
      g_nList = nList;
      if (nList == LIST_TEAMS && !g_bOpened) {
        g_bOpened = true;
        g_open.assign(Teams(), false);
        if (YourTeam() >= 0)
          g_open[YourTeam()] = true;
      }
    }
    Close();
  }
  Close();
  Decl save = Box("scsave");
  save.d.layout.sizing.width = Fixed(Sc(250));
  save.d.layout.sizing.height = Fixed(Sc(52));
  if (ScButton(save, SaveLabel(), 'S', false, CanSave()))
    SaveOpen();
  Decl ok = Box("scok");
  ok.d.layout.sizing.width = Fixed(Sc(250));
  ok.d.layout.sizing.height = Fixed(Sc(52));
  if (ScButton(ok, PortText("OK"), 'O', true))
    g_nResult = FRONT_SCORE_DONE;
  Close();
}

void SaveDialog(void)
{
  const bool bOut = DialogOpen("scsavedlg", 760, 280, 300);
  DialogTitle(PortText("SAVE REPLAY"));
  Decl e = Box("scsaveedit");
  e.d.layout.sizing.width = Grow();
  e.d.layout.sizing.height = Fixed(Sc(44));
  const std::string was = g_saveName;
  ScEdit(e, &g_saveName, 60, PortText("The replay's name"), true);
  if (g_saveName != was)
    g_bReplace = false;
  if (!g_saveNote.empty())
    Text(Keep(g_saveNote), UI_FACE_TEXT, Fs(15), 0.0f, kScInkAccept);
  Decl row;
  row.d.layout.sizing.width = Grow();
  row.d.layout.childGap = Gap(Fs(12));
  row.d.layout.childAlignment.x = CLAY_ALIGN_X_CENTER;
  Open(row);
  Decl yes = Box("scsaveyes");
  yes.d.layout.sizing.width = Fixed(Sc(220));
  yes.d.layout.sizing.height = Fixed(Sc(52));
  const bool bYes = ScButton(yes, PortText("Save"), 0, true);
  Decl no = Box("scsaveno");
  no.d.layout.sizing.width = Fixed(Sc(220));
  no.d.layout.sizing.height = Fixed(Sc(52));
  const bool bNo = ScButton(no, PortText("Cancel"), 0, false);
  Close();
  Close();
  if (bYes)
    Save();
  else if (bNo || bOut)
    g_bSaveDlg = false;
}

void Cycle(int *pn, int nCount)
{
  *pn = (*pn + 1) % nCount;
}

}  // namespace

namespace FrontKit {

void ScoreOpen(const ScoreGame &g, bool bFromHome)
{
  g_g = g;
  g_bFromHome = bFromHome;
  g_nResult = 0;
  g_nTab = SCORE_OVERVIEW;
  g_nSortCol = -1;
  g_nSortDir = 1;
  g_mark.assign(Players(), false);
  g_open.assign(Teams(), false);
  g_bOpened = false;
  g_nList = -1;
  g_fScroll[0] = g_fScroll[1] = 0.0f;
  g_bCentre = true;
  g_fIn = 0.0f;
  g_bSaveDlg = false;
  g_bSaved = false;
  CountStart(false);
  FrontLog("score: %d players, %d teams, %s, the local player's result %d%s\n", Players(), Teams(), g_g.map.c_str(),
           g_g.end.nLocal, g_g.bLeft ? ", left" : "");
}

void ScoreScreen(void)
{
  CountFrame();
  // the ground: the planet behind a dim (score.css .sco-dim), drawn before the layout's
  // elements and so under all of them
  UiFill(0.0f, 0.0f, static_cast<float>(UiWidth()), static_cast<float>(UiHeight()), kDim);

  const float fW = UiWidth() / Sc(1) - 116;
  const float fH = fminf(990.0f, UiHeight() / Sc(1) - 132 - 74);
  Decl panel = Box("scpanel");
  panel.d.layout.sizing.width = Fixed(Sc(fW));
  panel.d.layout.sizing.height = Fixed(Sc(fH));
  panel.d.floating.attachTo = CLAY_ATTACH_TO_ROOT;
  panel.d.floating.offset.x = Sc(58);
  panel.d.floating.offset.y = Sc(132);
  panel.d.floating.zIndex = 20;
  static UiCustom s_frame = {0.0f, DrawScFrame, NULL};
  panel.d.custom.customData = &s_frame;
  Open(panel);
  Head(fW);
  Tabs();
  Table(fW, fH - 112 - 33 - 52 - 14);
  Foot(fW, fH);
  Close();
  g_bCentre = false;
  if (g_bSaveDlg)
    SaveDialog();
  DrawTip();

  // the panel comes in from the left (score.css sco-in: 0.5 s)
  if (g_fIn < 0.5f) {
    static Uint64 s_qLast;
    const Uint64 qNow = SDL_GetTicks();
    g_fIn += g_fIn == 0.0f ? 0.001f : (std::min)(0.1f, (qNow - s_qLast) / 1000.0f);
    s_qLast = qNow;
    const float e = UiBezier(fminf(1.0f, g_fIn / 0.5f), 0.22f, 1.0f, 0.36f, 1.0f);
    UiMotion m = {e, -Sc(48) * (1.0f - e), 0.0f, 1.0f};
    UiMotionSet(Id("scpanel").id, m);
  }
}

bool ScoreKeys(void)
{
  if (g_bSaveDlg) {
    if (Key(VK_RETURN))
      Save();
    else if (Key(VK_ESCAPE))
      g_bSaveDlg = false;
    return true;
  }
  for (int t = 0; t < SCORE_TABS; ++t)
    if (Key(kTabKeys[t])) {
      SetTab(t);
      return true;
    }
  if (Key(VK_LEFT))
    SetTab((g_nTab + SCORE_TABS - 1) % SCORE_TABS);
  else if (Key(VK_RIGHT))
    SetTab((g_nTab + 1) % SCORE_TABS);
  else if (Key(VK_SPACE))
    CountFinish();
  else if (Key('D'))
    Cycle(&g_nDensity, 4);
  else if (Key('G') && Teams() > 0) {
    g_nList = (ListMode() + 1) % 3;
    if (g_nList == LIST_TEAMS && !g_bOpened) {
      g_bOpened = true;
      g_open.assign(Teams(), false);
      if (YourTeam() >= 0)
        g_open[YourTeam()] = true;
    }
  } else if (Key('S'))
    SaveOpen();
  else if (Key('O') || Key(VK_RETURN) || Key(VK_ESCAPE))
    g_nResult = FRONT_SCORE_DONE;
  return true;
}

int ScoreResult(void)
{
  return g_nResult;
}

void ScoreClose(void)
{
  CountFinish();
  stop_digital();
}

}  // namespace FrontKit
