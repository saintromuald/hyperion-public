// port_trainerview.cpp -- the trainer's screen: the playing strip, the Tape, the sheet and the
// lane figures. Presentation only: what changes the game
// goes through port_trainer.cpp. A layer over the game (port_overlay.h): the GPU presenter
// places the game in its area and draws this after it.
//
// While the game runs the keys take Ctrl, so 1.17's command keys stay the player's: Space
// pauses, R retries, Q sets a retry point, K keeps, B branches, M marks, T takes over a watched
// line. Paused, the game goes into a frame in the middle over 300 ms, the Tape comes round it,
// and the keys take no Ctrl: in a line's middle Enter tries again from the playhead, K as a
// timeline, X rewrites the line, W watches on; the arrows step a second (Shift ten), up and
// down change lines, [ and ] go to the marks, V picks the line the figures are held against,
// S lays the sheet over the frame and G draws a figure along the lanes.
#include <windows.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <algorithm>
#include <string>
#include <vector>
#include <SDL3/SDL.h>
#include "../loop/port_clock.h"  // PortGameSpeedTimes, PortGameSpeedTimesSet
#include "../platform/port_paths.h"  // PortDataFolder
#include "../render/port_present.h"  // PortPresentRedraw
#include "../render/port_view.h"  // g_portView
#include "../front/port_frontkit.h"   // the theme's colours
#include "port_trainer.h"
#include "../front/port_ui.h"
#include "CUnitMisc.h"           // get_unit_name_from_type
#include "../../h/typelimits.h"     // UNIT_TYPES_117
#include "../../gen/globals117.h"   // the units' costs and kinds
#include "../platform/port_globals117.h"
#include "../render/port_overlay.h"  // PORT_OVERLAY_LAYER

using namespace FrontKit;

namespace {

const char *const kDash = "\xe2\x80\x93";
const unsigned kGood = 0x7bd88f, kBad = 0xff8a65, kGold = 0xffe490, kThis = 0x66d5fe, kAttempt = 0x9aa6b2;
// a kept line's colour, by the order the timelines were made
const unsigned kLineColours[8] = {0x66d5fe, 0xc792ea, 0x7bd88f, 0xf0a35e, 0x4fc3d9, 0xd6d26b, 0xf07178, 0x8fb8de};

struct Rect {
  float x, y, w, h;
  bool In(float px, float py) const { return px >= x && py >= y && px < x + w && py < y + h; }
};

// Drawing in stage units, 1080 high and at least 1920 wide, as the benchmark view's.
struct Pen {
  float k;
};

float StageScale(float fW, float fH)
{
  return fW / fH >= 1920.0f / 1080.0f ? fH / 1080.0f : fW / 1920.0f;
}

void Fill(const Pen &p, const Rect &r, UiColor c, float fRadius = 0.0f)
{
  UiFill(r.x * p.k, r.y * p.k, r.w * p.k, r.h * p.k, c, fRadius * p.k);
}

void Ring(const Pen &p, const Rect &r, UiColor c, float fRadius, float fWidth)
{
  UiRing(r.x * p.k, r.y * p.k, r.w * p.k, r.h * p.k, c, fRadius * p.k, fWidth * p.k);
}

void Line(const Pen &p, float x0, float y0, float x1, float y1, UiColor c, float fWidth)
{
  UiLine(x0 * p.k, y0 * p.k, x1 * p.k, y1 * p.k, c, fWidth * p.k);
}

float Text(const Pen &p, float x, float y, float fSize, UiColor c, const char *psz, UiFace face = UI_FACE_TEXT)
{
  const float fSpace = face == UI_FACE_WIDE ? 0.06f * fSize : 0.0f;
  return UiText(x * p.k, y * p.k, face, fSize * p.k, fSpace * p.k, c, psz) / p.k;
}

float Width(const Pen &p, float fSize, const char *psz, UiFace face = UI_FACE_TEXT)
{
  const float fSpace = face == UI_FACE_WIDE ? 0.06f * fSize : 0.0f;
  return UiTextWidth(face, fSize * p.k, fSpace * p.k, psz) / p.k;
}

float TextIn(const Pen &p, float x, float y, float w, float fSize, UiColor c, const std::string &s,
             UiFace face = UI_FACE_TEXT)
{
  if (Width(p, fSize, s.c_str(), face) <= w)
    return Text(p, x, y, fSize, c, s.c_str(), face);
  std::string cut = s;
  while (!cut.empty() && Width(p, fSize, (cut + "\xe2\x80\xa6").c_str(), face) > w) {
    cut.pop_back();
    while (!cut.empty() && (static_cast<unsigned char>(cut.back()) & 0xC0) == 0x80)
      cut.pop_back();
    if (!cut.empty() && (static_cast<unsigned char>(cut.back()) & 0xC0) == 0xC0)
      cut.pop_back();
  }
  return Text(p, x, y, fSize, c, (cut + "\xe2\x80\xa6").c_str(), face);
}

float TextRight(const Pen &p, float xRight, float y, float fSize, UiColor c, const char *psz,
                UiFace face = UI_FACE_TEXT)
{
  return Text(p, xRight - Width(p, fSize, psz, face), y, fSize, c, psz, face);
}

// A diamond about (x, y), r from its middle to a corner.
void Diamond(const Pen &p, float x, float y, float r, UiColor c)
{
  UiSetTurn(3.14159265f / 4.0f, x * p.k, y * p.k);
  const float s = r * 1.4142f;
  Fill(p, {x - s / 2.0f, y - s / 2.0f, s, s}, c);
  UiSetTurn(0.0f, 0.0f, 0.0f);
}

void Dot(const Pen &p, float x, float y, float r, UiColor c)
{
  Fill(p, {x - r, y - r, 2.0f * r, 2.0f * r}, c, r);
}

// the screen's parts as drawn, target pixels: over them the pointer is off the game
Rect g_covers[16];
int g_nCovers;

void AddCover(const Pen &p, const Rect &r)
{
  if (g_nCovers < static_cast<int>(sizeof g_covers / sizeof g_covers[0]))
    g_covers[g_nCovers++] = {r.x * p.k, r.y * p.k, r.w * p.k, r.h * p.k};
}

void Panel(const Pen &p, const Rect &r)
{
  AddCover(p, r);
  Fill(p, r, UiRgb(0x040e1c, 0.86f), 3.6f);
  Ring(p, r, kLbInnerRim, 3.6f, 1.8f);
}

void Title(const Pen &p, float x, float y, const char *psz)
{
  Text(p, x, y, 13.5f, kLbInkInfo, psz, UI_FACE_WIDE);
}

enum Action {
  ACT_NONE, ACT_RESUME, ACT_RETRY, ACT_POINT, ACT_KEEP, ACT_BRANCH, ACT_MARK, ACT_LEAVE, ACT_LINE, ACT_KEEPLINE,
  ACT_POINTROW, ACT_CHOICE, ACT_TAPE, ACT_VS, ACT_SCOPE, ACT_ROW, ACT_SHEET, ACT_SHEETBG, ACT_TAB, ACT_GRAPHROW,
  ACT_LANEFIG, ACT_LEAVEASK, ACT_LEAVEBG, ACT_LEAVENAME
};
struct Button {
  Rect r;  // target pixels
  Action act;
  int n;
};
Button g_buttons[224];
int g_nButtons;
int g_nHot = -1;
bool g_bDrawn;
float g_k = 1.0f;

void AddHit(const Pen &p, const Rect &r, Action act, int n = 0)
{
  if (g_nButtons < static_cast<int>(sizeof g_buttons / sizeof g_buttons[0]))
    g_buttons[g_nButtons++] = {{r.x * p.k, r.y * p.k, r.w * p.k, r.h * p.k}, act, n};
}

bool Hot(Action act, int n = 0)
{
  return g_nHot >= 0 && g_nHot < g_nButtons && g_buttons[g_nHot].act == act && g_buttons[g_nHot].n == n;
}

enum Look { LOOK_PLAIN, LOOK_GO, LOOK_GOLD };

// A button as the design's sc-mini, its hot key's letter underlined; a key cap before the
// label when pszCap is given (the design's Space on Resume).
float Button(const Pen &p, float x, float y, float h, float fSize, const char *psz, Action act, Look look = LOOK_PLAIN,
             bool bOff = false, char cKey = 0, const char *pszCap = NULL, int n = 0)
{
  const float fCapW = pszCap != NULL ? Width(p, 11.0f, pszCap, UI_FACE_BOLD) + 12.0f + 8.0f : 0.0f;
  const float w = Width(p, fSize, psz, UI_FACE_BOLD) + 2.0f * 14.0f + fCapW;
  const Rect r = {x, y, w, h};
  const bool bHot = !bOff && Hot(act, n);
  const float fAlpha = bOff ? 0.45f : 1.0f;
  UiColor fill = bHot ? kLbItemHi : kLbItem;
  UiColor rim = kLbItemRim, ink = kLbInkInfo;
  if (look == LOOK_GO) {
    rim = kLbHyperionRim;
    ink = kScInkAccept;
  } else if (look == LOOK_GOLD) {
    rim = UiRgb(kGold, 0.9f);
    fill = bHot ? UiRgb(0x6a5520) : UiRgb(0x4a3c14);
    ink = UiRgb(kGold);
  }
  fill.a *= fAlpha;
  rim.a *= fAlpha;
  ink.a *= fAlpha;
  Fill(p, r, fill, 2.7f);
  Ring(p, r, rim, 2.7f, 1.0f);
  float tx = x + 14.0f;
  const float ty = y + h / 2.0f + fSize * 0.36f;
  if (pszCap != NULL) {
    const Rect cap = {tx, y + h / 2.0f - 10.0f, fCapW - 8.0f, 20.0f};
    Fill(p, cap, UiRgb(0x0c2238), 2.4f);
    Ring(p, cap, kLbItemRim, 2.4f, 1.0f);
    Text(p, cap.x + 6.0f, y + h / 2.0f + 4.0f, 11.0f, kLbInkDesc, pszCap, UI_FACE_BOLD);
    tx += fCapW;
  }
  Text(p, tx, ty, fSize, ink, psz, UI_FACE_BOLD);
  if (cKey != 0)
    for (const char *c = psz; *c; ++c)
      if ((*c & ~0x20) == (cKey & ~0x20)) {
        const std::string before(psz, c - psz);
        const char sz[2] = {*c, 0};
        const float ux = tx + Width(p, fSize, before.c_str(), UI_FACE_BOLD);
        Fill(p, {ux, ty + 2.0f, Width(p, fSize, sz, UI_FACE_BOLD), 1.2f}, ink);
        break;
      }
  if (!bOff)
    AddHit(p, r, act, n);
  return w;
}

std::string N0(long long n)
{
  char sz[32];
  const bool bNeg = n < 0;
  unsigned long long u = static_cast<unsigned long long>(bNeg ? -n : n);
  std::string s;
  int nDigits = 0;
  const std::string mark = PortTextC("thousands", ",");
  do {
    if (nDigits > 0 && nDigits % 3 == 0)
      s.insert(0, mark);
    s.insert(s.begin(), static_cast<char>('0' + u % 10));
    u /= 10;
    ++nDigits;
  } while (u != 0);
  snprintf(sz, sizeof sz, "%s%s", bNeg ? "-" : "", s.c_str());
  return sz;
}

std::string Signed(long long n)
{
  return n > 0 ? "+" + N0(n) : n == 0 ? "\xc2\xb1" "0" : N0(n);
}

// ---- the session as the view reads it ------------------------------------------------------------

bool g_bTape;             // paused into the Tape
bool g_bTapeOpening;      // the game going into its frame (or out of it)
Uint64 g_nMoveAt;         // when it began
bool g_bAsk;              // Space in a line's middle: the four ways on, called up
bool g_bLeaveAsk;         // Leave: how the session is saved, asked
bool g_bNameEdit;         // its Save as field takes the keys
std::string g_leaveName;  // the name it is saved under
int g_nVs = -1;           // the line the figures are held against, -1 for the default
bool g_bWholeGame;        // the figures over the whole game, not this try
bool g_bSheet;            // the sheet over the frame and the lists
int g_nSheetTab;          // its tab: Economy, Army, Mechanics, Build Order
int g_nGraph = -1;        // the row graphed under it, -1 its tab's first
int g_nLaneRow = -1;      // the figure drawn along the lanes, -1 the lines alone
std::string g_toastHead, g_toastLine;
Uint64 g_nToastUntil;
unsigned g_nLastFrame = ~0u;

void Toast(const std::string &said)
{
  const size_t n = said.find('\n');
  g_toastHead = said.substr(0, n);
  g_toastLine = n == std::string::npos ? std::string() : said.substr(n + 1);
  g_nToastUntil = SDL_GetTicks() + 1600;
}

const TrainerLine *Cur(void)
{
  return PortTrainerLine(PortTrainerCurrent());
}

unsigned Now(void)
{
  return oc_dwReplayFrame;
}

// a timeline, or a replay a session started from: saved with it
bool Kept(const TrainerLine &l)
{
  return l.nKind != TRAINER_ATTEMPT;
}

UiColor LineColour(const TrainerLine &l, float fAlpha = 1.0f)
{
  if (!Kept(l))
    return UiRgb(kAttempt, fAlpha * 0.85f);
  if (l.nKind == TRAINER_REPLAY)
    return UiRgb(0xe6edf3, fAlpha);
  int nIndex = 0;
  for (const TrainerLine &o : PortTrainerLines())
    if (Kept(o) && o.nOrder < l.nOrder)
      ++nIndex;
  return UiRgb(kLineColours[nIndex % 8], fAlpha);
}

// the lanes: the tree depth first, each child under its parent by where it split
struct Lane {
  const TrainerLine *pLine;
  int nDepth;
};

void Walk(int nParent, int nDepth, std::vector<Lane> *pOut)
{
  std::vector<const TrainerLine *> kids;
  for (const TrainerLine &l : PortTrainerLines())
    if (l.nParent == nParent)
      kids.push_back(&l);
  std::sort(kids.begin(), kids.end(), [](const TrainerLine *a, const TrainerLine *b) {
    return a->nSplit != b->nSplit ? a->nSplit < b->nSplit : a->nOrder < b->nOrder;
  });
  for (const TrainerLine *l : kids) {
    pOut->push_back({l, nDepth});
    Walk(l->nId, nDepth + 1, pOut);
  }
}

std::vector<Lane> Lanes(void)
{
  std::vector<Lane> out;
  Walk(0, 0, &out);
  // a line whose parent went with the attempts dropped is under the first
  for (const TrainerLine &l : PortTrainerLines()) {
    bool bIn = false;
    for (const Lane &x : out)
      bIn = bIn || x.pLine == &l;
    if (!bIn)
      out.push_back({&l, 1});
  }
  return out;
}

std::string FromName(const TrainerLine &l)
{
  const std::vector<TrainerPoint> &points = PortTrainerPoints();
  // {0} the retry point it began at, or the time it split at
  if (l.nPoint >= 0 && l.nPoint < static_cast<int>(points.size()))
    return PortTextF(" from {0}", {points[l.nPoint].name});
  if (l.nSplit > 0)
    return PortTextF(" from {0}", {PortTrainerTime(l.nSplit)});
  return std::string();
}

// The line the figures are held against: the one V picked, or the last other try from the same
// retry point, or the line it came from.
const TrainerLine *VsLine(void)
{
  const TrainerLine *me = Cur();
  if (me == NULL)
    return NULL;
  if (g_nVs >= 0 && g_nVs != me->nId) {
    const TrainerLine *l = PortTrainerLine(g_nVs);
    if (l != NULL)
      return l;
  }
  const TrainerLine *pBest = NULL;
  if (me->nPoint >= 0)
    for (const TrainerLine &l : PortTrainerLines())
      if (l.nId != me->nId && l.nPoint == me->nPoint && (pBest == NULL || l.nOrder > pBest->nOrder))
        pBest = &l;
  if (pBest == NULL)
    pBest = PortTrainerLine(me->nParent);
  // else the last made of the others that reach this moment
  if (pBest == NULL)
    for (const TrainerLine &l : PortTrainerLines())
      if (l.nId != me->nId && l.nEnd >= Now() && (pBest == NULL || l.nOrder > pBest->nOrder))
        pBest = &l;
  return pBest;
}

// The actions a line's figures hold up to a frame, for the APM: the seconds' counts.
int ApmAt(const TrainerLine &l, unsigned nFrame, bool bAverage)
{
  const size_t i = std::min<size_t>(nFrame / TRAINER_SECOND, l.seconds.empty() ? 0 : l.seconds.size() - 1);
  if (l.seconds.empty() || i < 10)
    return -1;
  if (bAverage)
    return l.seconds[i].nActions * 60 / static_cast<int>(i);
  const size_t nBack = std::min<size_t>(60, i);
  return (l.seconds[i].nActions - l.seconds[i - nBack].nActions) * 60 / static_cast<int>(nBack);
}

// ---- the figures --------------------------------------------------------------------------------
// A line's figures over a stretch of it, from a frame to the moment (trainer.js's numbers(), its
// ROWS and SER): what the seconds sampled as the line first played, and what it built.

enum Row {
  ROW_INCOME, ROW_MINED, ROW_SPENT, ROW_BANK, ROW_BANKAVG, ROW_SQ, ROW_WORKERS,
  ROW_SUPPLY, ROW_BLOCKED,
  ROW_ARMY, ROW_LEAD, ROW_MADE, ROW_LOST, ROW_KILLED, ROW_TRADE,
  ROW_APM, ROW_APMAVG, ROW_EAPM, ROW_RED, ROW_PEAK, ROW_COUNT
};
enum { GROUP_ECONOMY, GROUP_SUPPLY, GROUP_ARMY, GROUP_MECHANICS };
const char *const kGroups[4] = {PT_("ECONOMY"), PT_("SUPPLY"), PT_("ARMY"), PT_("MECHANICS")};
struct RowDef {
  int nGroup;
  const char *pszName;
  int nGood;     // which way is better, for the difference's colour: 1 more, -1 less, 0 neither
  bool bSeries;  // it has a course over time, drawn on the lanes and under the sheet
};
const RowDef kRows[ROW_COUNT] = {
  {GROUP_ECONOMY, PT_("Income"), 1, true},          {GROUP_ECONOMY, PT_("Mined"), 1, true},
  {GROUP_ECONOMY, PT_("Spent"), 1, false},          {GROUP_ECONOMY, PT_("Unspent now"), -1, true},
  {GROUP_ECONOMY, PT_("Unspent, average"), -1, false}, {GROUP_ECONOMY, PT_("Spending quotient"), 1, false},
  {GROUP_ECONOMY, PT_("Workers"), 1, true},         {GROUP_SUPPLY, PT_("Supply"), 1, true},
  {GROUP_SUPPLY, PT_("Supply blocked"), -1, true},  {GROUP_ARMY, PT_("Army value"), 1, true},
  {GROUP_ARMY, PT_("Against theirs"), 1, true},     {GROUP_ARMY, PT_("Units made"), 1, false},
  {GROUP_ARMY, PT_("Lost"), -1, true},              {GROUP_ARMY, PT_("Killed"), 1, true},
  {GROUP_ARMY, PT_("Trade"), 1, false},             {GROUP_MECHANICS, "APM", 0, true},
  {GROUP_MECHANICS, PT_("APM, average"), 0, false}, {GROUP_MECHANICS, "EAPM", 1, true},
  {GROUP_MECHANICS, PT_("Redundancy"), -1, false},  {GROUP_MECHANICS, PT_("Peak APM"), 0, false}};

// what was built, by kind: the economy (workers, supply, refineries, town halls), the army, and
// the rest (production and tech buildings)
enum { SPEND_ECONOMY, SPEND_PRODUCTION, SPEND_ARMY, SPEND_REST };
const char *const kSpends[4] = {PT_("Economy"), PT_("Production"), PT_("Army"), PT_("Research")};
const unsigned kSpendColours[4] = {0x4fc3d9, 0xd6d26b, 0xf0a35e, 0x8fb8de};

int SpendOf(unsigned t)
{
  if (t == 7 || t == 41 || t == 64 || t == 42 || t == 109 || t == 156 || t == 110 || t == 149 || t == 157 || t == 106 ||
      t == 131 || t == 154)
    return SPEND_ECONOMY;
  return t < UNIT_TYPES_117 && (gubUnitCountInfo[t] & 8) ? SPEND_ARMY : SPEND_PRODUCTION;
}

// minerals and gas a unit of the type is worth; units.dat prices zerglings and scourge a pair
double CostOf(unsigned t)
{
  const double n = guwUnitMineralsCost[t] + guwUnitGasesCost[t];
  return t == 37 || t == 47 ? n / 2.0 : n;
}

// a unit's name as a build order writes it: 1.17's, its race left off
std::string BuildName(unsigned t)
{
  const char *psz = get_unit_name_from_type(static_cast<WORD>(t));
  // {0} the unit's type, for one the game has no name for
  std::string s = psz != NULL ? psz : PortTextF("Unit {0}", {std::to_string(t)});
  for (const char *pszRace : {"Terran ", "Zerg ", "Protoss "})
    if (s.compare(0, strlen(pszRace), pszRace) == 0 && s.size() > strlen(pszRace))
      return s.substr(strlen(pszRace));
  return s;
}

bool WorkerType(unsigned t)
{
  return t == 7 || t == 41 || t == 64;
}

// shown in a build order: not a spider mine, a nuke, an interceptor or a scarab
bool BuildShown(unsigned t)
{
  return t != 13 && t != 14 && t != 73 && t != 85;
}

struct Nums {
  bool bOk = false;
  TrainerSecond s = {}, s0 = {};
  double income = 0, mined = 0, spent = 0, bank = 0, bankAvg = 0, sq = 0;
  bool bSq = false;
  double spend[4] = {};
  int wMade = 0, wLost = 0;
  double blocked = 0;
  int blocks = 0;
  double lead = 0;
  int made = 0, lost = 0, killed = 0;
  double lostV = 0, killedV = 0, trade = 0;
  bool bTrade = false;
  double apmNow = 0, apm = 0, eapm = 0, red = 0, peak = 0;
  bool bApm = false;
};

const TrainerSecond &Sec(const TrainerLine &l, long long i)
{
  return l.seconds[static_cast<size_t>(SDL_clamp(i, 0LL, static_cast<long long>(l.seconds.size()) - 1))];
}

// the minutes a span of the last minute counts for (at least 10 seconds), and per-minute over it
double SpanMinutes(long long i)
{
  return static_cast<double>(SDL_clamp(i, 10LL, 60LL)) / 60.0;
}

// a line has figures at f as far as it has been played (a replay's, to where it was taken over)
bool Reaches(const TrainerLine &l, unsigned f)
{
  return !l.seconds.empty() && f / TRAINER_SECOND <= l.seconds.size() && f >= l.nSplit;
}

Nums NumsOf(const TrainerLine &l, unsigned nFrom, unsigned f)
{
  Nums n;
  if (!Reaches(l, f))
    return n;
  const long long i1 = SDL_min(static_cast<long long>(f / TRAINER_SECOND), static_cast<long long>(l.seconds.size()) - 1);
  const long long i0 = SDL_min(static_cast<long long>(nFrom / TRAINER_SECOND), i1);
  n.s = Sec(l, i1);
  n.s0 = Sec(l, i0);
  const TrainerSecond &back = Sec(l, i1 - 60);
  const double fMinutes = SDL_max(1.0 / 60.0, static_cast<double>(f > nFrom ? f - nFrom : 0) / TRAINER_SECOND / 60.0);
  const double fSpan = SpanMinutes(i1);
  n.income = (n.s.nMined - back.nMined) / fSpan;
  n.mined = n.s.nMined - n.s0.nMined;
  n.bank = n.s.nMinerals + n.s.nGas;
  n.spent = n.mined - (n.bank - (n.s0.nMinerals + n.s0.nGas));
  double fBuilt = 0;
  for (const TrainerBuilt &b : l.built)
    if (b.nFrame > nFrom && b.nFrame <= f) {
      const double c = CostOf(b.wType);
      n.spend[SpendOf(b.wType)] += c;
      fBuilt += c;
      n.wMade += WorkerType(b.wType);
    }
  n.spend[SPEND_REST] = SDL_max(0.0, n.spent - fBuilt);
  double fBanked = 0;
  for (long long i = i0; i <= i1; i++)
    fBanked += Sec(l, i).nMinerals + Sec(l, i).nGas;
  n.bankAvg = fBanked / static_cast<double>(i1 - i0 + 1);
  // SQ = 35 (0.00137 I - ln U) + 240, I gathered a minute and U the average unspent: fitted to
  // SC2's economy and meaningless under a minute, so it waits for one
  n.bSq = fMinutes >= 1.0;
  if (n.bSq)
    n.sq = 35.0 * (0.00137 * (n.mined / fMinutes) - log(SDL_max(1.0, n.bankAvg))) + 240.0;
  n.wLost = n.s.nWorkersLost - n.s0.nWorkersLost;
  n.blocked = n.s.nBlocked - n.s0.nBlocked;
  n.blocks = n.s.nBlocks - n.s0.nBlocks;
  n.lead = n.s.nArmy - n.s.nTheirArmy;
  n.made = n.s.nMade - n.s0.nMade;
  n.lost = n.s.nLost - n.s0.nLost;
  n.killed = n.s.nKilled - n.s0.nKilled;
  n.lostV = n.s.nLostValue - n.s0.nLostValue;
  n.killedV = n.s.nKilledValue - n.s0.nKilledValue;
  n.bTrade = n.lostV > 0;
  if (n.bTrade)
    n.trade = n.killedV / n.lostV;
  const double fActs = n.s.nActions - n.s0.nActions, fEff = n.s.nEffective - n.s0.nEffective;
  n.bApm = i1 >= 10;
  n.apmNow = (n.s.nActions - back.nActions) / fSpan;
  n.apm = fActs / fMinutes;
  n.eapm = fEff / fMinutes;
  n.red = fActs > 0 ? 1.0 - fEff / fActs : 0.0;
  for (long long i = i0 + 30; i <= i1; i++)
    n.peak = SDL_max(n.peak, (Sec(l, i).nActions - Sec(l, i - 30).nActions) * 2.0);
  if (i1 - i0 < 30)
    n.peak = SDL_max(n.peak, n.apm);
  n.bOk = true;
  return n;
}

// a row's value, NAN where it has none yet
double RowValue(int row, const Nums &n)
{
  switch (row) {
    case ROW_INCOME: return n.income;
    case ROW_MINED: return n.mined;
    case ROW_SPENT: return n.spent;
    case ROW_BANK: return n.bank;
    case ROW_BANKAVG: return n.bankAvg;
    case ROW_SQ: return n.bSq ? n.sq : NAN;
    case ROW_WORKERS: return n.s.nWorkers;
    case ROW_SUPPLY: return n.s.nSupplyUsed;
    case ROW_BLOCKED: return n.blocked;
    case ROW_ARMY: return n.s.nArmy;
    case ROW_LEAD: return n.lead;
    case ROW_MADE: return n.made;
    case ROW_LOST: return n.lostV;
    case ROW_KILLED: return n.killedV;
    case ROW_TRADE: return n.bTrade ? n.trade : NAN;
    case ROW_APM: return n.bApm ? n.apmNow : NAN;
    case ROW_APMAVG: return n.bApm ? n.apm : NAN;
    case ROW_EAPM: return n.bApm ? n.eapm : NAN;
    case ROW_RED: return n.bApm ? n.red : NAN;
    case ROW_PEAK: return n.bApm ? n.peak : NAN;
    default: return NAN;
  }
}

std::string Seconds(double v)
{
  char sz[32];
  if (v < 10.0)
    snprintf(sz, sizeof sz, "%.1fs", v);
  else if (v < 60.0)
    snprintf(sz, sizeof sz, "%ds", static_cast<int>(v + 0.5));
  else
    return PortTrainerTime(static_cast<unsigned>(v * TRAINER_SECOND));
  return sz;
}

std::string RowText(int row, const Nums &n)
{
  const double v = RowValue(row, n);
  if (!n.bOk || isnan(v))
    return kDash;
  char sz[48];
  switch (row) {
    case ROW_SUPPLY:
      snprintf(sz, sizeof sz, "%d/%d", n.s.nSupplyUsed, n.s.nSupplyMax);
      return sz;
    case ROW_BLOCKED: return Seconds(v);
    case ROW_LEAD: return Signed(llround(v));
    case ROW_TRADE:
      snprintf(sz, sizeof sz, "%.2f\xc3\x97", v);
      return sz;
    case ROW_RED:
      snprintf(sz, sizeof sz, "%d%%", static_cast<int>(llround(v * 100.0)));
      return sz;
    default: return N0(llround(v));
  }
}

// what the sheet writes under a value
std::string RowSub(int row, const Nums &n)
{
  if (!n.bOk)
    return std::string();
  switch (row) {
    // {0} the minerals in the bank, {1} the gas
    case ROW_BANK: return PortTextF("{0} + {1} gas", {N0(n.s.nMinerals), N0(n.s.nGas)});
    // {0} the workers made, {1} the workers lost
    case ROW_WORKERS: return PortTextF("{0} made, {1} lost", {N0(n.wMade), N0(n.wLost)});
    case ROW_SUPPLY: return n.s.nSupplyMax >= 200 ? std::string(PortText("the most there is")) : std::string();
    case ROW_BLOCKED: return PortTextNF("{0} time", "{0} times", n.blocks, {N0(n.blocks)});
    case ROW_LEAD: return PortTextF("theirs {0}", {N0(n.s.nTheirArmy)});
    case ROW_LOST:
      return PortTextNF("{0} unit or building", "{0} units and buildings", n.lost, {N0(n.lost)});
    case ROW_KILLED:
      return PortTextNF("{0} unit or building", "{0} units and buildings", n.killed, {N0(n.killed)});
    default: return std::string();
  }
}

// the difference from another line's value, coloured by which way is better
std::string RowDelta(int row, const Nums &a, const Nums &b, UiColor *pColour)
{
  *pColour = kLbInkDesc;
  const double va = RowValue(row, a), vb = RowValue(row, b);
  if (!a.bOk || !b.bOk || isnan(va) || isnan(vb))
    return std::string();
  const double d = va - vb;
  const double fNone = row == ROW_TRADE || row == ROW_RED ? 0.005 : 0.5;
  if (fabs(d) < fNone)
    return "\xc2\xb1" "0";
  if (kRows[row].nGood != 0)
    *pColour = d * kRows[row].nGood > 0 ? UiRgb(kGood) : UiRgb(kBad);
  char sz[48];
  switch (row) {
    case ROW_BLOCKED: return (d > 0 ? "+" : "-") + Seconds(fabs(d));
    case ROW_TRADE:
      snprintf(sz, sizeof sz, "%+.2f", d);
      return sz;
    case ROW_RED:
      snprintf(sz, sizeof sz, "%+d", static_cast<int>(llround(d * 100.0)));
      return PortTextF("{0} pts", {sz});
    default: return Signed(llround(d));
  }
}

// a row's course: its value at each game second i of a line
double SeriesAt(int row, const TrainerLine &l, long long i)
{
  const TrainerSecond &s = Sec(l, i), &back = Sec(l, i - 60);
  switch (row) {
    case ROW_INCOME: return (s.nMined - back.nMined) / SpanMinutes(i);
    case ROW_MINED: return s.nMined;
    case ROW_BANK: return s.nMinerals + s.nGas;
    case ROW_WORKERS: return s.nWorkers;
    case ROW_SUPPLY: return s.nSupplyUsed;
    case ROW_BLOCKED: return s.nBlocked;
    case ROW_ARMY: return s.nArmy;
    case ROW_LEAD: return s.nArmy - s.nTheirArmy;
    case ROW_LOST: return s.nLostValue;
    case ROW_KILLED: return s.nKilledValue;
    case ROW_APM: return (s.nActions - back.nActions) / SpanMinutes(i);
    case ROW_EAPM: return (s.nEffective - back.nEffective) / SpanMinutes(i);
    default: return 0.0;
  }
}

// where this try began: its retry point, or where it split
unsigned TryFrom(const TrainerLine &l)
{
  const std::vector<TrainerPoint> &points = PortTrainerPoints();
  if (l.nPoint >= 0 && l.nPoint < static_cast<int>(points.size()))
    return points[l.nPoint].nFrame;
  return l.nSplit;
}

// the scope the figures are over: this try, or the whole game
unsigned ScopeFrom(const TrainerLine &l)
{
  return g_bWholeGame ? 0u : TryFrom(l);
}

// the lines with this moment of their own, the one shown first, then the one held against
std::vector<const TrainerLine *> LinesAt(unsigned f)
{
  std::vector<const TrainerLine *> out;
  const TrainerLine *me = Cur(), *vs = VsLine();
  if (me != NULL)
    out.push_back(me);
  if (vs != NULL && vs != me && Reaches(*vs, f))
    out.push_back(vs);
  std::vector<const TrainerLine *> rest;
  for (const Lane &x : Lanes())
    if (x.pLine != me && x.pLine != vs && Reaches(*x.pLine, f))
      rest.push_back(x.pLine);
  std::sort(rest.begin(), rest.end(), [](const TrainerLine *a, const TrainerLine *b) { return a->nOrder > b->nOrder; });
  out.insert(out.end(), rest.begin(), rest.end());
  return out;
}

// what the lanes draw: the lines alone, or a figure along each
struct LaneFigure {
  int nRow;  // -1 the lines alone
  const char *pszName;
};
const LaneFigure kLaneFigures[6] = {{-1, PT_("Lines")},           {ROW_INCOME, PT_("Income")},
                                    {ROW_ARMY, PT_("Army value")}, {ROW_LEAD, PT_("Against theirs")},
                                    {ROW_APM, "APM"},             {ROW_BLOCKED, PT_("Supply blocks")}};

// ---- the layout -----------------------------------------------------------------------------------

struct Layout {
  float k, fW, fH;
  Rect top, frame, left, right, tape;
};

Layout LayoutFor(float fW, float fH)
{
  Layout l;
  l.k = StageScale(fW, fH);
  l.fW = fW / l.k;
  l.fH = fH / l.k;
  l.top = {16.0f, 14.0f, l.fW - 32.0f, 62.0f};
  // the frame the game's own shape, 640 by 480 or the wide view's, as high as the Tape leaves
  const float fAspect = g_portView.nHeight > 0 ? static_cast<float>(g_portView.nWidth) / g_portView.nHeight : 4.0f / 3.0f;
  float fFrameH = 618.0f, fFrameW = SDL_floorf(fFrameH * fAspect);
  const float fMostW = l.fW - 2.0f * 16.0f - 2.0f * (330.0f + 14.0f);
  if (fFrameW > fMostW) {
    fFrameW = fMostW;
    fFrameH = SDL_floorf(fFrameW / fAspect);
  }
  l.frame = {SDL_floorf((l.fW - fFrameW) / 2.0f), 92.0f, fFrameW, fFrameH};
  l.left = {16.0f, l.frame.y, l.frame.x - 16.0f - 14.0f, l.frame.h};
  const float fRightX = l.frame.x + l.frame.w + 14.0f;
  l.right = {fRightX, l.frame.y, l.fW - 16.0f - fRightX, l.frame.h};
  const float fTapeY = l.frame.y + l.frame.h + 14.0f;
  l.tape = {16.0f, fTapeY, l.fW - 32.0f, l.fH - 16.0f - fTapeY};
  return l;
}

// how far the game is into its frame, 0 playing to 1 paused, eased over 300 ms
float FrameIn(void)
{
  const float t = SDL_min(1.0f, (SDL_GetTicks() - g_nMoveAt) / 300.0f);
  if (t >= 1.0f)
    g_bTapeOpening = false;
  const float e = UiBezier(t, 0.2f, 0.7f, 0.2f, 1.0f);
  return g_bTape ? e : 1.0f - e;
}

// ---- playing: the strip -----------------------------------------------------------------------------

std::string RetryName(void)
{
  const int n = PortTrainerRetryPoint();
  const std::vector<TrainerPoint> &points = PortTrainerPoints();
  return n >= 0 ? points[n].name : std::string(PortText("The start"));
}

// At the game's top, past the timer's corner and short of 1.17's resources at the right.
void Strip(const Pen &p, const Rect &game)
{
  const TrainerLine *me = Cur();
  if (me == NULL)
    return;
  const bool bWatch = PortTrainerState() != TRAINER_LIVE;
  const float fLeft = game.x + 96.0f;
  const float w = SDL_max(360.0f, SDL_min(820.0f, game.w * 0.62f - 96.0f));
  const Rect r = {fLeft, game.y + 8.0f, w, 62.0f};
  Panel(p, r);
  float x = r.x + 14.0f;
  const float y = r.y + 22.0f;
  if (!bWatch) {
    Dot(p, x + 5.0f, y - 4.0f, 5.0f, UiRgb(0xf05050));
    x += 16.0f;
  }
  // the replay a session started from says so while it plays
  const char *pszState = !bWatch                        ? PortText("RECORDING")
                         : me->nKind == TRAINER_REPLAY ? PortText("REPLAY")
                                                       : PortText("WATCHING");
  x += Text(p, x, y, 13.0f, bWatch ? UiRgb(kThis) : UiRgb(0xf08080), pszState, UI_FACE_WIDE);
  x += 18.0f;
  x += Text(p, x, y, 15.0f, kLbInkInfo, (me->name + FromName(*me)).c_str(), UI_FACE_BOLD);
  x += 14.0f;
  const std::string clock = PortTrainerTime(Now()) +
                            (PortGameSpeedTimes() > 1 ? std::string(kDot) + "\xc3\x97" + std::to_string(PortGameSpeedTimes()) : std::string());
  Text(p, x, y, 14.0f, kLbInkDesc, clock.c_str(), UI_FACE_BOLD);
  // the keys with what they do; each key's own letter stays as it is
  std::string keys = PortText("Ctrl with: Space Pause") + std::string(kDot) +
                     PortTextF("R Retry from {0}", {RetryName()}) + kDot + PortText("Q Point") + kDot +
                     PortText("K Keep") + kDot + PortText("B Branch") + kDot + PortText("M Mark") + kDot +
                     PortText("+ \xe2\x88\x92 Speed");
  if (bWatch)
    keys += std::string(kDot) + PortText("T Take over");
  TextIn(p, r.x + 14.0f, r.y + 41.0f, r.w - 28.0f, 12.0f, kLbInkDesc, keys);
  // the ribbon: the session's length, this line along it, the retry point and now
  unsigned nMax = TRAINER_SECOND * 60;
  for (const TrainerLine &o : PortTrainerLines())
    nMax = SDL_max(nMax, o.nEnd + TRAINER_SECOND * 10);
  const float x0 = r.x + 14.0f, x1 = r.x + r.w - 14.0f, ry = r.y + r.h - 9.0f;
  auto X = [&](unsigned f) { return x0 + (x1 - x0) * static_cast<float>(f) / static_cast<float>(nMax); };
  Line(p, x0, ry, x1, ry, UiRgb(0x2a4a6a, 0.6f), 2.0f);
  Line(p, X(me->nSplit), ry, X(SDL_max(me->nEnd, Now())), ry, LineColour(*me), 3.0f);
  const int nPoint = PortTrainerRetryPoint();
  if (nPoint >= 0)
    Diamond(p, X(PortTrainerPoints()[nPoint].nFrame), ry, 5.0f, UiRgb(kGold));
  Dot(p, X(Now()), ry, 4.0f, UiRgb(0xffffff));
}

// ---- paused: the Tape ----------------------------------------------------------------------------------

void TopBar(const Pen &p, const Layout &l)
{
  const TrainerLine *me = Cur();
  Panel(p, l.top);
  const Rect badge = {l.top.x + 14.0f, l.top.y + 17.0f, 86.0f, 28.0f};
  Fill(p, badge, UiRgb(0x4a3c14), 3.0f);
  Ring(p, badge, UiRgb(kGold, 0.8f), 3.0f, 1.0f);
  const char *const pszPaused = PortText("PAUSED");
  Text(p, badge.x + (badge.w - Width(p, 13.0f, pszPaused, UI_FACE_WIDE)) / 2.0f, badge.y + 19.0f, 13.0f, UiRgb(kGold),
       pszPaused, UI_FACE_WIDE);
  const bool bMid = me != NULL && Now() < me->nEnd;
  // the buttons, from the right
  const float h = 34.0f, by = l.top.y + (l.top.h - h) / 2.0f;
  struct B {
    const char *psz;
    Action act;
    char cKey;
    bool bOff;
  };
  const bool bKept = me == NULL || Kept(*me);
  const B bs[] = {{PortText("Retry"), ACT_RETRY, 'R', false}, {PortText("Retry Point"), ACT_POINT, 'Q', false},
                  {PortText("Keep"), ACT_KEEP, 'K', bKept}, {PortText("Branch"), ACT_BRANCH, 'B', false},
                  {PortText("Mark"), ACT_MARK, 'M', false}, {PortText("Leave"), ACT_LEAVE, 0, false}};
  // widths first, to lay them from the right
  float fAll = 0.0f;
  const char *pszResume = bMid ? PortText("Resume\xe2\x80\xa6") : PortText("Resume");
  const float fResumeW = Width(p, 14.0f, pszResume, UI_FACE_BOLD) + 28.0f + Width(p, 11.0f, "Space", UI_FACE_BOLD) + 20.0f;
  fAll += fResumeW + 8.0f;
  for (const B &b : bs)
    fAll += Width(p, 14.0f, b.psz, UI_FACE_BOLD) + 28.0f + 8.0f;
  float x = l.top.x + l.top.w - 14.0f - fAll + 8.0f;
  x += Button(p, x, by, h, 14.0f, pszResume, ACT_RESUME, LOOK_GO, false, 0, "Space") + 8.0f;
  for (const B &b : bs)
    x += Button(p, x, by, h, 14.0f, b.psz, b.act, LOOK_PLAIN, b.bOff, b.cKey) + 8.0f;
  // what is shown
  if (me == NULL)
    return;
  const float tx = badge.x + badge.w + 16.0f, tw = l.top.x + l.top.w - 14.0f - fAll - tx - 12.0f;
  TextIn(p, tx, l.top.y + 28.0f, tw, 17.0f, kLbInkInfo, me->name + FromName(*me), UI_FACE_BOLD);
  // {0} where the line is now, {1} where it ends
  std::string sub = (bMid ? PortTextF("At {0} of {1} on this line", {PortTrainerTime(Now()), PortTrainerTime(me->nEnd)})
                          : PortTextF("At {0}, its end", {PortTrainerTime(Now())})) +
                    kDot +
                    (me->nKind == TRAINER_REPLAY ? PortText("the replay, as recorded")
                     : Kept(*me)                 ? PortText("a timeline, saved")
                                                 : PortText("an attempt, held: saved if you save every line as you leave"));
  unsigned nSnap = 0, nFrames = 0;
  PortTrainerLastSeek(&nSnap, &nFrames);
  // {0} the snapshot's time, {1} the frames played on from it
  if (nSnap != 0 || nFrames != 0)
    sub += std::string(kDot) +
           PortTextF("from the snapshot at {0} and {1} frames on", {PortTrainerTime(nSnap), N0(nFrames)});
  // a saved session this build plays differently than the one that made it
  if (PortTrainerDiffered() > 0)
    sub = PortText("This session plays differently in this build than when it was made") + std::string(kDot) + sub;
  TextIn(p, tx, l.top.y + 48.0f, tw, 13.0f, PortTrainerDiffered() > 0 ? UiRgb(0xff8a65) : kLbInkDesc, sub);
}

// The left column: the player's figures at this moment over the try or the whole game, each
// against another line; a figure with a course over time draws on the lanes when picked.
void Figures(const Pen &p, const Layout &l)
{
  const Rect &r = l.left;
  Panel(p, r);
  const TrainerLine *me = Cur();
  if (me == NULL)
    return;
  const unsigned f = Now();
  float y = r.y + 30.0f;
  // {0} the moment the figures are taken at
  Title(p, r.x + 14.0f, y, PortTextF("AT {0}", {PortTrainerTime(f)}).c_str());
  // the scope: this try, from its retry point or split, or the whole game
  y += 12.0f;
  const unsigned nTry = TryFrom(*me);
  const std::string tryName =
      nTry > 0 ? PortTextF("This try, from {0}", {PortTrainerTime(nTry)}) : std::string(PortText("This try"));
  const float fSegW = (r.w - 28.0f) / 2.0f;
  for (int i = 0; i < 2; ++i) {
    const Rect b = {r.x + 14.0f + fSegW * i, y, fSegW - (i == 0 ? 2.0f : 0.0f), 24.0f};
    const bool bOn = (i == 1) == g_bWholeGame;
    Fill(p, b, bOn ? UiRgb(0x0c2a44) : Hot(ACT_SCOPE, i) ? kLbItemHi : kLbItem, 2.4f);
    Ring(p, b, bOn ? kLbHyperionRim : kLbItemRim, 2.4f, 1.0f);
    const std::string s = i == 0 ? tryName : std::string(PortText("Whole game"));
    const float fW = SDL_min(Width(p, 12.0f, s.c_str()), b.w - 12.0f);
    TextIn(p, b.x + (b.w - fW) / 2.0f, b.y + 16.0f, b.w - 12.0f, 12.0f, bOn ? kLbInkInfo : kLbInkDesc, s);
    AddHit(p, b, ACT_SCOPE, i);
  }
  y += 50.0f;
  const TrainerLine *vs = VsLine();
  const unsigned nFrom = ScopeFrom(*me);
  const Nums a = NumsOf(*me, nFrom, f);
  const bool bVs = vs != NULL && Reaches(*vs, f);
  const Nums b = bVs ? NumsOf(*vs, nFrom, f) : Nums();
  // against: V cycles it
  const Rect vsBox = {r.x + 14.0f, y - 16.0f, r.w - 28.0f, 24.0f};
  Fill(p, vsBox, Hot(ACT_VS) ? kLbItemHi : kLbItem, 2.4f);
  Ring(p, vsBox, kLbItemRim, 2.4f, 1.0f);
  float vx = vsBox.x + 8.0f;
  vx += Text(p, vx, y + 1.0f, 11.0f, kLbInkDesc, "V", UI_FACE_BOLD) + 8.0f;
  vx += Text(p, vx, y + 1.0f, 12.5f, kLbInkDesc, PortText("Against")) + 8.0f;
  if (vs != NULL) {
    Fill(p, {vx, y - 8.0f, 9.0f, 9.0f}, LineColour(*vs), 1.5f);
    vx += 15.0f;
  }
  TextIn(p, vx, y + 1.0f, vsBox.x + vsBox.w - vx - 8.0f, 12.5f, kLbInkInfo,
         // {0} the line's name, when it does not reach this moment
         vs != NULL ? (bVs ? vs->name : PortTextF("{0} (not this far)", {vs->name}))
                    : std::string(PortText("no other line")),
         UI_FACE_BOLD);
  AddHit(p, vsBox, ACT_VS);
  y += 26.0f;
  if (!a.bOk) {
    Text(p, r.x + 14.0f, y, 13.0f, kLbInkDesc, PortText("The first second is still to come."));
    return;
  }
  const float xVal = r.x + r.w - 86.0f, xDelta = r.x + r.w - 14.0f;
  const Rect allBtn = {r.x + 14.0f, r.y + r.h - 38.0f, r.w - 28.0f, 28.0f};
  int nGroup = -1;
  for (int row = 0; row < ROW_COUNT; ++row) {
    if (kRows[row].nGroup != nGroup) {
      nGroup = kRows[row].nGroup;
      y += 6.0f;
      Text(p, r.x + 14.0f, y, 11.0f, kLbInkDesc, PortText(kGroups[nGroup]), UI_FACE_WIDE);
      y += 17.0f;
    }
    if (y > allBtn.y - 4.0f)
      break;
    const Rect hit = {r.x + 8.0f, y - 13.0f, r.w - 16.0f, 17.0f};
    const bool bPicked = kRows[row].bSeries && g_nLaneRow == row;
    if (bPicked || (kRows[row].bSeries && Hot(ACT_ROW, row)))
      Fill(p, hit, bPicked ? UiRgb(kThis, 0.12f) : UiRgb(0x0a1e32, 0.9f), 2.0f);
    Text(p, r.x + 14.0f, y, 13.0f, bPicked ? UiRgb(kThis) : kLbInkDesc, PortText(kRows[row].pszName));
    TextRight(p, xVal, y, 13.0f, kLbInkInfo, RowText(row, a).c_str(), UI_FACE_BOLD);
    if (bVs) {
      UiColor c;
      const std::string d = RowDelta(row, a, b, &c);
      TextRight(p, xDelta, y, 11.5f, c, d.c_str());
    }
    if (kRows[row].bSeries)
      AddHit(p, hit, ACT_ROW, row);
    y += 18.0f;
  }
  Fill(p, allBtn, Hot(ACT_SHEET) ? kLbItemHi : kLbItem, 2.4f);
  Ring(p, allBtn, kLbItemRim, 2.4f, 1.0f);
  const char *const pszAll = PortText("All Stats");
  const float fAllW = Width(p, 13.5f, pszAll, UI_FACE_BOLD);
  const float ax = allBtn.x + (allBtn.w - fAllW) / 2.0f;
  Text(p, ax, allBtn.y + 19.0f, 13.5f, kLbInkInfo, pszAll, UI_FACE_BOLD);
  // S underlined where the label has it, as a button's key is
  for (const char *c = pszAll; *c != '\0'; ++c)
    if ((*c & ~0x20) == 'S') {
      const char szKey[2] = {*c, 0};
      Fill(p, {ax + Width(p, 13.5f, std::string(pszAll, c - pszAll).c_str(), UI_FACE_BOLD), allBtn.y + 21.0f,
               Width(p, 13.5f, szKey, UI_FACE_BOLD), 1.2f},
           kLbInkInfo);
      break;
    }
  AddHit(p, allBtn, ACT_SHEET);
}

// a list's row: a line, or a retry point
float LineRow(const Pen &p, const Rect &r, float y, const TrainerLine &line, bool bOn)
{
  const Rect row = {r.x + 10.0f, y, r.w - 20.0f, 42.0f};
  if (bOn || Hot(ACT_LINE, line.nId)) {
    Fill(p, row, bOn ? UiRgb(0x0c2a44, 0.9f) : UiRgb(0x0a1e32, 0.9f), 2.4f);
    Ring(p, row, bOn ? kLbHyperionRim : kLbItemRim, 2.4f, 1.0f);
  }
  Fill(p, {row.x + 10.0f, y + 11.0f, 9.0f, 9.0f}, LineColour(line), 1.5f);
  TextIn(p, row.x + 26.0f, y + 19.0f, row.w - 100.0f, 13.5f, kLbInkInfo, line.name, UI_FACE_BOLD);
  TrainerSecond s = {};
  std::string z = (line.nSplit > 0 ? PortTrainerTime(line.nSplit) : std::string("0:00")) + kDash +
                  PortTrainerTime(line.nEnd);
  // {0} the units lost on the line, {1} the units it killed
  if (PortTrainerSecondAt(line.nId, line.nEnd, &s))
    z += std::string(kDot) + PortTextF("lost {0}, killed {1}", {N0(s.nLost), N0(s.nKilled)});
  if (line.nResult != TRAINER_RESULT_NONE)
    z += std::string(kDot) + (line.nResult == TRAINER_RESULT_WON ? PortText("you won") : PortText("you lost"));
  TextIn(p, row.x + 26.0f, y + 35.0f, row.w - 36.0f, 11.5f, kLbInkDesc, z);
  if (!Kept(line)) {
    const Rect keep = {row.x + row.w - 64.0f, y + 6.0f, 56.0f, 18.0f};
    Fill(p, keep, Hot(ACT_KEEPLINE, line.nId) ? kLbItemHi : kLbItem, 2.0f);
    Ring(p, keep, kLbItemRim, 2.0f, 1.0f);
    const char *const pszKeep = PortText("KEEP");
    Text(p, keep.x + (keep.w - Width(p, 10.5f, pszKeep, UI_FACE_WIDE)) / 2.0f, keep.y + 13.0f, 10.5f, kLbInkInfo,
         pszKeep, UI_FACE_WIDE);
    AddHit(p, keep, ACT_KEEPLINE, line.nId);
  }
  AddHit(p, row, ACT_LINE, line.nId);
  return 46.0f;
}

void SideLists(const Pen &p, const Layout &l)
{
  const Rect &r = l.right;
  Panel(p, r);
  const TrainerLine *me = Cur();
  float y = r.y + 30.0f;
  const float yEnd = r.y + r.h - 8.0f;
  Title(p, r.x + 14.0f, y, PortText("TIMELINES"));
  y += 12.0f;
  for (const TrainerLine &line : PortTrainerLines())
    if (Kept(line) && y + 42.0f < yEnd)
      y += LineRow(p, r, y, line, me != NULL && me->nId == line.nId);
  std::vector<const TrainerLine *> held;
  for (const TrainerLine &line : PortTrainerLines())
    if (!Kept(line))
      held.push_back(&line);
  std::sort(held.begin(), held.end(), [](const TrainerLine *a, const TrainerLine *b) { return a->nOrder > b->nOrder; });
  y += 18.0f;
  // {0} how many attempts are held
  Title(p, r.x + 14.0f, y,
        PortTextF("ATTEMPTS \xc2\xb7 {0}", {std::to_string(static_cast<int>(held.size()))}).c_str());
  y += 12.0f;
  if (held.empty()) {
    y += 16.0f;
    TextIn(p, r.x + 14.0f, y, r.w - 28.0f, 12.0f, kLbInkDesc,
           PortText("None yet. R tries again from the retry point."));
    y += 8.0f;
  }
  for (const TrainerLine *line : held)
    if (y + 42.0f < yEnd)
      y += LineRow(p, r, y, *line, me != NULL && me->nId == line->nId);
  y += 18.0f;
  if (y + 30.0f < yEnd)
    Title(p, r.x + 14.0f, y, PortText("RETRY POINTS"));
  y += 12.0f;
  const std::vector<TrainerPoint> &points = PortTrainerPoints();
  const int nRetry = PortTrainerRetryPoint();
  for (int i = 0; i < static_cast<int>(points.size()) && y + 36.0f < yEnd; ++i) {
    const Rect row = {r.x + 10.0f, y, r.w - 20.0f, 36.0f};
    if (i == nRetry || Hot(ACT_POINTROW, i)) {
      Fill(p, row, i == nRetry ? UiRgb(0x2a2410, 0.9f) : UiRgb(0x0a1e32, 0.9f), 2.4f);
      Ring(p, row, i == nRetry ? UiRgb(kGold, 0.6f) : kLbItemRim, 2.4f, 1.0f);
    }
    Diamond(p, row.x + 14.0f, y + 13.0f, 5.0f, UiRgb(kGold));
    TextIn(p, row.x + 26.0f, y + 17.0f, row.w - 36.0f, 13.0f, kLbInkInfo, points[i].name, UI_FACE_BOLD);
    TextIn(p, row.x + 26.0f, y + 31.0f, row.w - 36.0f, 11.5f, kLbInkDesc,
           PortTrainerTime(points[i].nFrame) + kDot +
               PortTextNF("{0} try", "{0} tries", points[i].nTries, {N0(points[i].nTries)}));
    AddHit(p, row, ACT_POINTROW, i);
    y += 40.0f;
  }
}

// the tape's geometry as last drawn, stage units
struct TapeGeom {
  Rect area;  // the lanes and the ruler
  float x0, x1, fRuler, fLane;
  unsigned nMax;
  std::vector<Lane> lanes;
  float X(unsigned f) const { return x0 + (x1 - x0) * static_cast<float>(f) / static_cast<float>(nMax); }
  unsigned F(float x) const
  {
    const float t = SDL_clamp((x - x0) / (x1 - x0), 0.0f, 1.0f);
    return static_cast<unsigned>(t * static_cast<float>(nMax) + 0.5f);
  }
};
TapeGeom g_tape;
Rect g_choiceBox;  // the four ways on, stage units, when shown

TapeGeom TapeFor(const Rect &area)
{
  TapeGeom g;
  g.area = area;
  g.lanes = Lanes();
  unsigned nEnd = 0;
  for (const TrainerLine &l : PortTrainerLines())
    nEnd = SDL_max(nEnd, l.nEnd);
  nEnd = SDL_max(nEnd, Now());
  const unsigned nHalf = TRAINER_SECOND * 30;
  g.nMax = SDL_max(TRAINER_SECOND * 60, (nEnd + TRAINER_SECOND * 20 + nHalf - 1) / nHalf * nHalf);
  g.x0 = area.x + 160.0f;
  g.x1 = area.x + area.w - 16.0f;
  g.fRuler = 26.0f;
  g.fLane = SDL_clamp((area.h - g.fRuler - 60.0f) / SDL_max(1.0f, static_cast<float>(g.lanes.size())), 18.0f, 32.0f);
  return g;
}

float LaneY(const TapeGeom &g, size_t i)
{
  return g.area.y + g.fRuler + g.fLane * static_cast<float>(i) + g.fLane / 2.0f;
}

int RowOf(const TapeGeom &g, int nId)
{
  for (size_t i = 0; i < g.lanes.size(); ++i)
    if (g.lanes[i].pLine->nId == nId)
      return static_cast<int>(i);
  return -1;
}

// a figure along each lane, over its own stretch: an area under the line from the lane's foot
// (its middle, for one that goes below nothing); supply blocks as red bars
void LaneFigures(const Pen &p, const TapeGeom &g)
{
  const int row = g_nLaneRow;
  if (row < 0)
    return;
  double top = 1.0;
  for (const Lane &x : g.lanes) {
    const TrainerLine &b = *x.pLine;
    for (long long i = b.nSplit / TRAINER_SECOND; i < static_cast<long long>(b.seconds.size()); i += 2)
      top = SDL_max(top, fabs(SeriesAt(row, b, i)));
  }
  const bool bMid = row == ROW_LEAD;
  const float fSecW = g.X(TRAINER_SECOND) - g.X(0);
  const long long nStep = SDL_max(1LL, static_cast<long long>(ceilf(2.0f / SDL_max(0.01f, fSecW))));
  for (size_t li = 0; li < g.lanes.size(); ++li) {
    const TrainerLine &b = *g.lanes[li].pLine;
    const float y = LaneY(g, li), half = g.fLane / 2.0f - 2.0f;
    const long long i0 = b.nSplit / TRAINER_SECOND, i1 = static_cast<long long>(b.seconds.size()) - 1;
    if (i1 <= i0)
      continue;
    const UiColor c = Kept(b) ? LineColour(b) : UiRgb(kAttempt);
    if (row == ROW_BLOCKED) {
      for (long long j = SDL_max(1LL, i0); j <= i1; ++j)
        if (b.seconds[static_cast<size_t>(j)].nBlocked > b.seconds[static_cast<size_t>(j - 1)].nBlocked)
          Fill(p, {g.X(static_cast<unsigned>((j - 1) * TRAINER_SECOND)), y - half + 1.0f, SDL_max(2.0f, fSecW), half * 2.0f - 2.0f}, UiRgb(0xff6b5a, 0.85f));
      continue;
    }
    const float base = bMid ? y : y + half, k = static_cast<float>((bMid ? half : half * 2.0f) / top);
    UiColor area = c;
    area.a *= 0.3f;
    float lx = 0.0f, ly = 0.0f;
    for (long long i = i0; i <= i1; i += nStep) {
      const float vy = base - static_cast<float>(SeriesAt(row, b, i)) * k;
      const float bx = g.X(static_cast<unsigned>(i * TRAINER_SECOND));
      const float bw = SDL_max(1.0f, fSecW * nStep) + 0.6f;
      Fill(p, {bx, SDL_min(vy, base), bw, fabsf(base - vy)}, area);
      if (i > i0)
        Line(p, lx, ly, bx, vy, c, 1.2f);
      lx = bx;
      ly = vy;
    }
  }
}

const char *const kChoiceKeys[4] = {"Enter", "K", "X", "W"};

void Tape(const Pen &p, const Layout &l)
{
  const Rect &r = l.tape;
  Panel(p, r);
  Title(p, r.x + 14.0f, r.y + 26.0f, PortText("TIMELINE"));
  // what the lanes draw: G
  {
    float fx = r.x + 150.0f;
    const Rect cap = {fx, r.y + 12.0f, 20.0f, 20.0f};
    Fill(p, cap, UiRgb(0x0c2238), 2.4f);
    Ring(p, cap, kLbItemRim, 2.4f, 1.0f);
    Text(p, cap.x + 6.0f, cap.y + 14.5f, 11.0f, kLbInkDesc, "G", UI_FACE_BOLD);
    fx += 26.0f;
    for (int i = 0; i < 6; ++i) {
      const char *const pszFig = PortText(kLaneFigures[i].pszName);
      const float w = Width(p, 12.0f, pszFig, UI_FACE_BOLD) + 20.0f;
      const Rect b = {fx, r.y + 10.0f, w, 24.0f};
      const bool bOn = kLaneFigures[i].nRow == g_nLaneRow;
      Fill(p, b, bOn ? UiRgb(0x0c2a44) : Hot(ACT_LANEFIG, i) ? kLbItemHi : kLbItem, 2.4f);
      Ring(p, b, bOn ? kLbHyperionRim : kLbItemRim, 2.4f, 1.0f);
      Text(p, fx + 10.0f, b.y + 16.5f, 12.0f, bOn ? kLbInkInfo : kLbInkDesc, pszFig, UI_FACE_BOLD);
      AddHit(p, b, ACT_LANEFIG, i);
      fx += w + 3.0f;
    }
  }
  // {0} how many times the game's own pace it plays at
  TextRight(p, r.x + r.w - 14.0f, r.y + 26.0f, 12.0f, kLbInkDesc,
            PortTextF("Drag to scrub \xc2\xb7 \xe2\x86\x90\xe2\x86\x92 a second, Shift ten \xc2\xb7 [ ] marks \xc2\xb7 "
                      "\xe2\x86\x91\xe2\x86\x93 lines \xc2\xb7 Enter tries again \xc2\xb7 + \xe2\x88\x92 speed \xc3\x97{0}",
                      {std::to_string(PortGameSpeedTimes())})
                .c_str());
  const Rect area = {r.x + 8.0f, r.y + 38.0f, r.w - 16.0f, r.h - 46.0f};
  g_tape = TapeFor(area);
  const TapeGeom &g = g_tape;
  const TrainerLine *me = Cur();
  const unsigned f = Now();
  UiPushClip(area.x * p.k, area.y * p.k, (area.x + area.w) * p.k, (area.y + area.h) * p.k);
  // the ruler: a tick every 10 seconds, the minutes named
  for (unsigned t = 0; t <= g.nMax; t += TRAINER_SECOND * 10) {
    const float X = g.X(t);
    const bool bMinute = t % (TRAINER_SECOND * 60) == 0;
    Line(p, X, area.y + (bMinute ? 4.0f : 14.0f), X, area.y + area.h, UiRgb(0xa0c8ff, bMinute ? 0.30f : 0.10f), 1.0f);
    if (bMinute)
      Text(p, X + 4.0f, area.y + 12.0f, 11.5f, UiRgb(0xc8e1ff, 0.75f), PortTrainerTime(t).c_str());
  }
  // the lanes, over the figure along them when one is picked
  LaneFigures(p, g);
  const float fThin = g_nLaneRow >= 0 ? 0.5f : 1.0f;
  for (size_t i = 0; i < g.lanes.size(); ++i) {
    const TrainerLine &b = *g.lanes[i].pLine;
    const float y = LaneY(g, i);
    const bool bOn = me != NULL && b.nId == me->nId;
    if (bOn)
      Fill(p, {area.x, y - g.fLane / 2.0f, area.w, g.fLane}, UiRgb(kThis, 0.07f));
    const UiColor c = LineColour(b);
    const float fIndent = 8.0f + 8.0f * static_cast<float>(SDL_min(3, g.lanes[i].nDepth));
    TextIn(p, area.x + fIndent, y + 4.5f, g.x0 - area.x - fIndent - 8.0f, 12.5f, Kept(b) ? c : UiRgb(0xbec8d4, 0.85f),
           (Kept(b) ? std::string() : std::string("\xc2\xb7 ")) + b.name, bOn ? UI_FACE_BOLD : UI_FACE_TEXT);
    const float xs = g.X(b.nSplit), xe = SDL_max(xs + 14.0f, g.X(b.nEnd));
    const float fWidth = (bOn ? 5.0f : Kept(b) ? 4.0f : 2.5f) * fThin;
    const int nParentRow = RowOf(g, b.nParent);
    if (nParentRow >= 0) {
      // the split: a curve down from the parent's lane, (xs, py) to (xs + 14, y)
      const float py = LaneY(g, static_cast<size_t>(nParentRow));
      float lx = xs, ly = py;
      for (int s = 1; s <= 10; ++s) {
        const float t = s / 10.0f, u = 1.0f - t;
        const float bx = u * u * u * xs + 3 * u * u * t * (xs + 10.0f) + 3 * u * t * t * xs + t * t * t * (xs + 14.0f);
        const float by = u * u * u * py + 3 * u * u * t * py + 3 * u * t * t * y + t * t * t * y;
        Line(p, lx, ly, bx, by, c, fWidth);
        lx = bx;
        ly = by;
      }
      Line(p, xs + 14.0f, y, xe, y, c, fWidth);
    } else {
      Line(p, xs, y, xe, y, c, fWidth);
    }
    // its end, and its marks
    const UiColor cap = b.nResult == TRAINER_RESULT_WON ? UiRgb(kGood) : b.nResult == TRAINER_RESULT_LOST ? UiRgb(kBad) : c;
    Dot(p, xe, y, bOn ? 4.5f : 3.5f, cap);
    for (const TrainerMark &m : b.marks) {
      if (m.nFrame < b.nSplit)
        continue;
      const UiColor mc = m.nKind == TRAINER_MARK_YOURS                                 ? UiRgb(kGold)
                         : m.nKind == TRAINER_MARK_FIGHT                               ? UiRgb(0xffffff, 0.7f)
                         : m.nKind == TRAINER_MARK_WAVE                                ? UiRgb(kBad)
                         : m.nKind == TRAINER_MARK_END && m.nWho == TRAINER_RESULT_WON ? UiRgb(kGood)
                                                                                       : UiRgb(0xf07178);
      Diamond(p, g.X(m.nFrame), y, 5.0f, mc);
    }
  }
  // retry points: diamonds on the ruler, a dashed line down to their lane
  const std::vector<TrainerPoint> &points = PortTrainerPoints();
  for (const TrainerPoint &pt : points) {
    const int nRow = RowOf(g, pt.nLine);
    if (nRow < 0)
      continue;
    const float X = g.X(pt.nFrame);
    Diamond(p, X, area.y + 20.0f, 6.0f, UiRgb(kGold, 0.85f));
    const float ry = LaneY(g, static_cast<size_t>(nRow));
    for (float yy = area.y + 26.0f; yy < ry - 3.0f; yy += 5.0f)
      Line(p, X, yy, X, SDL_min(yy + 2.0f, ry), UiRgb(kGold, 0.35f), 1.0f);
  }
  // after the playhead on this line: what going on from here would leave or drop
  const int nMyRow = me != NULL ? RowOf(g, me->nId) : -1;
  const float hx = g.X(f), hy = nMyRow >= 0 ? LaneY(g, static_cast<size_t>(nMyRow)) : area.y + g.fRuler;
  if (me != NULL && f < me->nEnd)
    Fill(p, {hx, hy - g.fLane / 2.0f + 2.0f, g.X(me->nEnd) - hx, g.fLane - 4.0f}, UiRgb(kGold, 0.10f));
  // the playhead
  Line(p, hx, area.y, hx, area.y + area.h, UiRgb(0xffffff), 2.0f);
  Dot(p, hx, hy, 6.0f, UiRgb(0xffffff));
  const std::string clock = PortTrainerTime(f);
  const float cw = Width(p, 11.5f, clock.c_str(), UI_FACE_BOLD) + 12.0f;
  Fill(p, {hx - cw / 2.0f, area.y, cw, 15.0f}, UiRgb(0xffffff), 2.0f);
  Text(p, hx - cw / 2.0f + 6.0f, area.y + 12.0f, 11.5f, UiRgb(0x000000), clock.c_str(), UI_FACE_BOLD);
  UiPopClip();
  AddHit(p, area, ACT_TAPE);
  // the four ways on, hanging from the playhead in a line's middle
  g_choiceBox = {0, 0, 0, 0};
  if (me != NULL && f < me->nEnd) {
    const char *const apsz[4] = {PortText("Try again from here"), PortText("Try again, as a timeline"),
                                 PortText("Rewrite"), PortText("Watch what happened")};
    // {0} the line's name
    std::string rewrite = PortTextF("Rewrite {0} from here", {me->name});
    const char *const apszShown[4] = {apsz[0], apsz[1], rewrite.c_str(), apsz[3]};
    float fW = 12.0f;
    for (int i = 0; i < 4; ++i)
      fW += Width(p, 11.0f, kChoiceKeys[i], UI_FACE_BOLD) + 16.0f + Width(p, 13.0f, apszShown[i], UI_FACE_BOLD) + 24.0f + 6.0f;
    const float fH = 34.0f;
    const float fLanesEnd = area.y + g.fRuler + g.fLane * static_cast<float>(g.lanes.size()) + 6.0f;
    const float bx = SDL_clamp(hx - 22.0f, g.x0, area.x + area.w - fW - 4.0f);
    const float by = SDL_max(fLanesEnd, area.y + area.h - fH - 2.0f);
    g_choiceBox = {bx, by, fW, fH};
    Fill(p, g_choiceBox, UiRgb(0x030b16, 0.94f), 3.0f);
    Ring(p, g_choiceBox, g_bAsk ? UiRgb(kGold, 0.9f) : kLbItemRim, 3.0f, g_bAsk ? 1.6f : 1.0f);
    float x = bx + 6.0f;
    for (int i = 0; i < 4; ++i) {
      const float fKeyW = Width(p, 11.0f, kChoiceKeys[i], UI_FACE_BOLD) + 12.0f;
      const float w = fKeyW + 4.0f + Width(p, 13.0f, apszShown[i], UI_FACE_BOLD) + 24.0f;
      const Rect c = {x, by + 4.0f, w, fH - 8.0f};
      const bool bMain = i == 0;
      Fill(p, c, Hot(ACT_CHOICE, i) ? kLbItemHi : bMain ? UiRgb(0x0c2a44) : kLbItem, 2.4f);
      Ring(p, c, bMain ? kLbHyperionRim : kLbItemRim, 2.4f, 1.0f);
      const Rect key = {c.x + 6.0f, c.y + 5.0f, fKeyW, c.h - 10.0f};
      Fill(p, key, UiRgb(0x0c2238), 2.0f);
      Text(p, key.x + 6.0f, key.y + key.h - 5.0f, 11.0f, kLbInkDesc, kChoiceKeys[i], UI_FACE_BOLD);
      Text(p, key.x + key.w + 8.0f, c.y + c.h / 2.0f + 5.0f, 13.0f, bMain ? kScInkAccept : kLbInkInfo, apszShown[i],
           UI_FACE_BOLD);
      AddHit(p, c, ACT_CHOICE, i);
      x += w + 6.0f;
    }
  }
}

// ---- the sheet: every line at this moment side by side ---------------------------------------------
// (trainer.js's fillSheet, stackBar, buildOrders and drawSheetGraph) It lies over the frame and the
// lists; S or Esc closes it.

const char *const kTabs[4] = {PT_("Economy"), PT_("Army"), PT_("Mechanics"), PT_("Build Order")};

bool RowOnTab(int row, int nTab)
{
  const int g = kRows[row].nGroup;
  return nTab == 0 ? g == GROUP_ECONOMY || g == GROUP_SUPPLY : nTab == 1 ? g == GROUP_ARMY : nTab == 2 && g == GROUP_MECHANICS;
}

void SpendBar(const Pen &p, float x, float y, float w, const Nums &n)
{
  double fAll = 0;
  for (double v : n.spend)
    fAll += v;
  Fill(p, {x, y, w, 6.0f}, UiRgb(0x1a2a3c), 2.0f);
  float bx = x;
  std::string pct;
  for (int k = 0; k < 4; ++k) {
    const float bw = fAll > 0 ? static_cast<float>(w * n.spend[k] / fAll) : 0.0f;
    if (bw > 0.5f)
      Fill(p, {bx, y, bw, 6.0f}, UiRgb(kSpendColours[k]));
    bx += bw;
    char sz[16];
    snprintf(sz, sizeof sz, "%s%d", k ? " \xc2\xb7 " : "", fAll > 0 ? static_cast<int>(llround(100.0 * n.spend[k] / fAll)) : 0);
    pct += sz;
  }
  Text(p, x, y + 19.0f, 11.0f, kLbInkDesc, (pct + " %").c_str());
}

// a game second's value on the graph, lines of a row
void SheetGraph(const Pen &p, const Rect &g, int row, const std::vector<const TrainerLine *> &lines, unsigned f, unsigned nFrom)
{
  long long nMaxI = 60;
  double lo = 0.0, hi = 1.0;
  for (const TrainerLine *l : lines) {
    nMaxI = SDL_max(nMaxI, static_cast<long long>(l->seconds.size()) - 1);
    for (long long i = 0; i < static_cast<long long>(l->seconds.size()); i += 2) {
      const double v = SeriesAt(row, *l, i);
      lo = SDL_min(lo, v);
      hi = SDL_max(hi, v);
    }
  }
  const float x0 = g.x + 56.0f, x1 = g.x + g.w - 130.0f, y0 = g.y + 10.0f, y1 = g.y + g.h - 22.0f;
  auto X = [&](double i) { return x0 + static_cast<float>(i / static_cast<double>(nMaxI)) * (x1 - x0); };
  auto Y = [&](double v) { return y1 - static_cast<float>((v - lo) / (hi - lo)) * (y1 - y0); };
  // the scope, shaded, and the minutes
  Fill(p, {X(nFrom / static_cast<double>(TRAINER_SECOND)), y0, X(f / static_cast<double>(TRAINER_SECOND)) - X(nFrom / static_cast<double>(TRAINER_SECOND)), y1 - y0},
       UiRgb(kGold, 0.06f));
  for (long long m = 0; m * 60 <= nMaxI; ++m) {
    Line(p, X(static_cast<double>(m * 60)), y0, X(static_cast<double>(m * 60)), y1, UiRgb(0xa0c8ff, 0.14f), 1.0f);
    Text(p, X(static_cast<double>(m * 60)) - 12.0f, y1 + 16.0f, 11.0f, kLbInkDesc, PortTrainerTime(static_cast<unsigned>(m * 60 * TRAINER_SECOND)).c_str());
  }
  if (lo < 0.0)
    Line(p, x0, Y(0.0), x1, Y(0.0), UiRgb(0xa0c8ff, 0.3f), 1.0f);
  TextRight(p, x0 - 8.0f, y0 + 8.0f, 11.0f, kLbInkDesc, N0(llround(hi)).c_str());
  TextRight(p, x0 - 8.0f, y1, 11.0f, kLbInkDesc, N0(llround(lo)).c_str());
  // each line, the one shown last, on top
  float aLabelY[8];
  int nLabels = 0;
  for (size_t k = lines.size(); k-- > 0;) {
    const TrainerLine &l = *lines[k];
    const UiColor c = LineColour(l);
    const long long nTo = static_cast<long long>(l.seconds.size()) - 1;
    const long long nStep = SDL_max(1LL, nMaxI / 400);
    float lx = X(0.0), ly = Y(SeriesAt(row, l, 0));
    for (long long i = nStep; i <= nTo; i += nStep) {
      const float nx = X(static_cast<double>(i)), ny = Y(SeriesAt(row, l, i));
      Line(p, lx, ly, nx, ny, c, k == 0 ? 2.0f : 1.4f);
      lx = nx;
      ly = ny;
    }
    // its name at its end, kept clear of the others'
    float ty = ly + 4.0f;
    for (int j = 0; j < nLabels; ++j)
      if (fabsf(aLabelY[j] - ty) < 13.0f)
        ty = aLabelY[j] + 13.0f;
    if (nLabels < 8)
      aLabelY[nLabels++] = ty;
    TextIn(p, lx + 6.0f, ty, x1 + 124.0f - lx - 6.0f, 11.5f, c, l.name);
  }
  // the moment
  Line(p, X(f / static_cast<double>(TRAINER_SECOND)), y0, X(f / static_cast<double>(TRAINER_SECOND)), y1, UiRgb(0xffffff, 0.7f), 1.0f);
}

// Leave asks how the session is saved, and under what name: every line, the tries too; its
// timelines only; not at all. -1 is Cancel. A session opened from its file and not changed
// since leaves without asking.
void LeaveAs(int nSave)
{
  g_bLeaveAsk = false;
  g_bNameEdit = false;
  if (nSave < 0)
    return;
  g_bTape = false;
  PortTrainerLeave(nSave, g_leaveName);
}

void LeaveOpen(void)
{
  if (!PortTrainerChanged()) {
    LeaveAs(TRAINER_SAVE_NONE);
    return;
  }
  g_leaveName = PortTrainerSetupNow().name;
  g_bLeaveAsk = true;
  g_bNameEdit = false;
}

// What the session holds: its timelines, its tries, its retry points and the marks set.
void Holds(int *pnTimelines, int *pnTries, int *pnPoints, int *pnMarks)
{
  *pnTimelines = *pnTries = *pnMarks = 0;
  for (const TrainerLine &k : PortTrainerLines()) {
    (k.nKind == TRAINER_ATTEMPT ? *pnTries : *pnTimelines)++;
    for (const TrainerMark &m : k.marks)
      *pnMarks += m.nKind == TRAINER_MARK_YOURS && m.nFrame >= k.nSplit;
  }
  *pnPoints = static_cast<int>(PortTrainerPoints().size());
}

std::string Timelines(int n)
{
  return PortTextNF("{0} timeline", "{0} timelines", n, {std::to_string(n)});
}

std::string Tries(int n)
{
  return PortTextNF("{0} try", "{0} tries", n, {std::to_string(n)});
}

void LeaveAsk(const Pen &p, const Layout &l)
{
  // over everything: a click outside the box does nothing
  AddHit(p, {0.0f, 0.0f, l.fW, l.fH}, ACT_LEAVEBG);
  Fill(p, {0.0f, 0.0f, l.fW, l.fH}, UiRgb(0x000000, 0.45f));
  int nTimelines, nTries, nPoints, nMarks;
  Holds(&nTimelines, &nTries, &nPoints, &nMarks);
  const TrainerSetup &setup = PortTrainerSetupNow();
  const bool bOpened = !setup.open.empty();
  // where it goes: the path under the data folder, and the replay's copy beside it
  std::string path = PortTrainerSavePath(g_leaveName);
  const std::string data = PortDataFolder();
  if (path.compare(0, data.size(), data) == 0)
    path = path.substr(data.size());
  std::string where = path;
  // {0} the replay's file name
  if (!setup.replay.empty() && setup.replay.find("\\Trainer\\Sessions\\") == std::string::npos) {
    const size_t nSlash = setup.replay.find_last_of("\\/");
    where += PortTextF(", and {0} copied beside it",
                       {nSlash == std::string::npos ? setup.replay : setup.replay.substr(nSlash + 1)});
  }
  struct Way {
    const char *pszKey, *pszLabel;
    std::string line;
    int nSave;
    bool bOff;
  };
  // {0} the timelines it holds, {1} the tries
  const Way ways[4] = {
      {"S", PortText("Save Session"),
       nTries > 0 ? PortTextF("Every line: {0} and {1}, the points and marks", {Timelines(nTimelines), Tries(nTries)})
                  : PortTextF("Every line: {0}, the points and marks", {Timelines(nTimelines)}),
       TRAINER_SAVE_ALL, false},
      {"T", PortText("Save Timelines Only"),
       nTries > 0 ? PortText("The timelines with their points and marks; the tries not kept are let go")
                  : PortText("The same as Save Session here: there are no tries"),
       TRAINER_SAVE_TIMELINES, nTries == 0},
      {"L", PortText("Leave Without Saving"),
       bOpened ? PortText("Its file stays as it was when it was opened") : PortText("Nothing is kept"),
       TRAINER_SAVE_NONE, false},
      {"C", PortText("Cancel"), PortText("Back to the session, still paused"), -1, false}};
  const float w = 620.0f, rowH = 54.0f, h = 146.0f + 4.0f * (rowH + 6.0f) + 34.0f;
  const Rect r = {(l.fW - w) / 2.0f, (l.fH - h) / 2.0f, w, h};
  AddCover(p, r);
  Fill(p, r, UiRgb(0x030b16), 3.6f);
  Ring(p, r, kLbHyperionRim, 3.6f, 1.6f);
  AddHit(p, r, ACT_LEAVEBG);
  Text(p, r.x + 20.0f, r.y + 34.0f, 15.0f, kLbInkInfo, PortText("LEAVE THIS SESSION"), UI_FACE_WIDE);
  // {0} its timelines, {1} its tries, {2} its retry points, {3} the marks set
  const std::string holds =
      PortTextF("It holds {0}, {1}, {2} and {3}",
                {Timelines(nTimelines), Tries(nTries),
                 PortTextNF("{0} retry point", "{0} retry points", nPoints, {std::to_string(nPoints)}),
                 PortTextNF("{0} mark", "{0} marks", nMarks, {std::to_string(nMarks)})}) +
      kDot + (bOpened ? PortText("changed since it was saved") : PortText("not saved yet"));
  TextIn(p, r.x + 20.0f, r.y + 60.0f, w - 40.0f, 13.0f, kLbInkDesc, holds);
  // Save as: the name, a click to type
  Text(p, r.x + 20.0f, r.y + 96.0f, 13.0f, kLbInkInfo, PortText("Save as"), UI_FACE_BOLD);
  const Rect field = {r.x + 96.0f, r.y + 76.0f, w - 116.0f, 30.0f};
  Fill(p, field, UiRgb(0x0c2238), 2.4f);
  Ring(p, field, g_bNameEdit ? UiRgb(kGold, 0.9f) : kLbItemRim, 2.4f, g_bNameEdit ? 1.6f : 1.0f);
  const float fNameW = Text(p, field.x + 10.0f, field.y + 21.0f, 14.0f, kLbInkInfo, g_leaveName.c_str(), UI_FACE_BOLD);
  if (g_bNameEdit)
    Line(p, field.x + 12.0f + fNameW, field.y + 7.0f, field.x + 12.0f + fNameW, field.y + 24.0f, UiRgb(kGold), 1.5f);
  AddHit(p, field, ACT_LEAVENAME);
  TextIn(p, r.x + 96.0f, r.y + 126.0f, w - 116.0f, 12.0f, kLbInkDesc, where);
  float y = r.y + 146.0f;
  for (int i = 0; i < 4; ++i) {
    const Way &way = ways[i];
    const Rect c = {r.x + 16.0f, y, w - 32.0f, rowH};
    const bool bMain = i == 0;
    Fill(p, c, !way.bOff && Hot(ACT_LEAVEASK, way.nSave) ? kLbItemHi : bMain ? UiRgb(0x0c2a44) : kLbItem, 2.4f);
    Ring(p, c, bMain ? kLbHyperionRim : kLbItemRim, 2.4f, 1.0f);
    const float fKeyW = Width(p, 11.0f, way.pszKey, UI_FACE_BOLD) + 12.0f;
    const Rect key = {c.x + 10.0f, c.y + 15.0f, fKeyW, 22.0f};
    Fill(p, key, UiRgb(0x0c2238), 2.0f);
    Text(p, key.x + 6.0f, key.y + 16.0f, 11.0f, kLbInkDesc, way.pszKey, UI_FACE_BOLD);
    const float tx = c.x + 10.0f + 40.0f;
    Text(p, tx, c.y + 23.0f, 14.0f, way.bOff ? UiRgb(0x6b7785) : bMain ? kScInkAccept : kLbInkInfo, way.pszLabel, UI_FACE_BOLD);
    Text(p, tx, c.y + 42.0f, 12.0f, way.bOff ? UiRgb(0x6b7785) : kLbInkDesc, way.line.c_str());
    if (!way.bOff)
      AddHit(p, c, ACT_LEAVEASK, way.nSave);
    y += rowH + 6.0f;
  }
  Text(p, r.x + 20.0f, r.y + h - 16.0f, 11.5f, kLbInkDesc,
       PortText("Enter saves every line. Leaving by the Game Menu saves every line too."));
}

void Sheet(const Pen &p, const Layout &l)
{
  const Rect r = {l.frame.x, l.frame.y, l.right.x + l.right.w - l.frame.x, l.frame.h};
  AddCover(p, r);
  Fill(p, r, UiRgb(0x030b16), 3.6f);
  Ring(p, r, kLbHyperionRim, 3.6f, 1.6f);
  AddHit(p, r, ACT_SHEETBG);
  const TrainerLine *me = Cur();
  if (me == NULL)
    return;
  const unsigned f = Now(), nFrom = ScopeFrom(*me);
  std::vector<const TrainerLine *> lines = LinesAt(f);
  if (lines.size() > 6)
    lines.resize(6);
  // the head: what this is, the tabs, Close
  float y = r.y + 30.0f;
  float x = r.x + 16.0f;
  // {0} the moment the figures are taken at
  x += Text(p, x, y, 15.0f, kLbInkInfo, PortTextF("STATS AT {0}", {PortTrainerTime(f)}).c_str(), UI_FACE_WIDE) + 16.0f;
  // {0} where the stretch begins, {1} the line's name, {2} the moment every line must reach
  const std::string fine =
      g_bWholeGame || nFrom == 0
          ? PortTextF("From the start, for every line that reaches {0}", {PortTrainerTime(f)})
          : PortTextF("From {0}, where {1} began, for every line that reaches {2}",
                      {PortTrainerTime(nFrom), me->name, PortTrainerTime(f)});
  const float fClose = Width(p, 13.5f, PortText("Close"), UI_FACE_BOLD) + 28.0f;
  float fTabsW = 0.0f;
  for (const char *t : kTabs)
    fTabsW += Width(p, 12.5f, PortText(t), UI_FACE_BOLD) + 22.0f + 4.0f;
  const float tabX = r.x + r.w - 16.0f - fClose - 16.0f - fTabsW;
  TextIn(p, x, y, tabX - x - 12.0f, 12.0f, kLbInkDesc, fine);
  float tx = tabX;
  for (int i = 0; i < 4; ++i) {
    const char *const pszTab = PortText(kTabs[i]);
    const float w = Width(p, 12.5f, pszTab, UI_FACE_BOLD) + 22.0f;
    const Rect b = {tx, y - 19.0f, w, 26.0f};
    const bool bOn = i == g_nSheetTab;
    Fill(p, b, bOn ? UiRgb(0x0c2a44) : Hot(ACT_TAB, i) ? kLbItemHi : kLbItem, 2.4f);
    Ring(p, b, bOn ? kLbHyperionRim : kLbItemRim, 2.4f, 1.0f);
    Text(p, tx + 11.0f, y - 1.0f, 12.5f, bOn ? kLbInkInfo : kLbInkDesc, pszTab, UI_FACE_BOLD);
    AddHit(p, b, ACT_TAB, i);
    tx += w + 4.0f;
  }
  Button(p, r.x + r.w - 16.0f - fClose, y - 19.0f, 26.0f, 13.5f, PortText("Close"), ACT_SHEET, LOOK_PLAIN, false, 'S');
  // the columns: a line each
  y += 22.0f;
  const float fLabelW = 170.0f;
  const float cx0 = r.x + 16.0f + fLabelW;
  const float cw = (r.x + r.w - 16.0f - cx0) / static_cast<float>(SDL_max(static_cast<size_t>(1), lines.size()));
  for (size_t k = 0; k < lines.size(); ++k) {
    const TrainerLine &line = *lines[k];
    const Rect h = {cx0 + cw * k, y, cw - 8.0f, 40.0f};
    if (Hot(ACT_LINE, line.nId))
      Fill(p, h, UiRgb(0x0a1e32, 0.9f), 2.4f);
    Fill(p, {h.x + 4.0f, h.y + 9.0f, 9.0f, 9.0f}, LineColour(line), 1.5f);
    TextIn(p, h.x + 19.0f, h.y + 17.0f, h.w - 22.0f, 13.5f, k == 0 ? UiRgb(kThis) : kLbInkInfo, line.name, UI_FACE_BOLD);
    const std::vector<TrainerPoint> &points = PortTrainerPoints();
    const std::string from = line.nPoint >= 0 && line.nPoint < static_cast<int>(points.size()) ? points[line.nPoint].name : PortTrainerTime(line.nSplit);
    TextIn(p, h.x + 19.0f, h.y + 33.0f, h.w - 22.0f, 11.0f, kLbInkDesc, from + kDash + PortTrainerTime(line.nEnd));
    AddHit(p, h, ACT_LINE, line.nId);
  }
  y += 48.0f;
  Line(p, r.x + 16.0f, y - 4.0f, r.x + r.w - 16.0f, y - 4.0f, UiRgb(0xa0c8ff, 0.18f), 1.0f);
  if (g_nSheetTab == 3) {
    // the build orders, as a player writes one: the time, the supply, what came to be
    std::vector<std::vector<const TrainerBuilt *>> lists(lines.size());
    size_t nRows = 0;
    for (size_t k = 0; k < lines.size(); ++k) {
      for (const TrainerBuilt &b : lines[k]->built)
        if (b.nFrame <= f && BuildShown(b.wType))
          lists[k].push_back(&b);
      nRows = SDL_max(nRows, lists[k].size());
    }
    const size_t nFit = static_cast<size_t>(SDL_max(1.0f, (r.y + r.h - 14.0f - y) / 19.0f));
    // the latest that fit, so the moment's end of it shows
    const size_t nFirst = nRows > nFit ? nRows - nFit : 0;
    if (nRows == 0)
      Text(p, r.x + 16.0f, y + 14.0f, 13.0f, kLbInkDesc, PortText("Nothing built yet on these lines."));
    for (size_t i = nFirst; i < nRows; ++i) {
      const float ry = y + 14.0f + 19.0f * static_cast<float>(i - nFirst);
      TextRight(p, r.x + 16.0f + 40.0f, ry, 12.0f, kLbInkDesc, N0(static_cast<long long>(i + 1)).c_str());
      for (size_t k = 0; k < lines.size(); ++k) {
        if (i >= lists[k].size())
          continue;
        const TrainerBuilt &b = *lists[k][i];
        const float bx = cx0 + cw * k;
        const TrainerBuilt *mine = i < lists[0].size() ? lists[0][i] : NULL;
        if (k > 0 && mine != NULL && mine->wType != b.wType)
          Fill(p, {bx - 2.0f, ry - 13.0f, cw - 8.0f, 17.0f}, UiRgb(kBad, 0.10f), 2.0f);
        Text(p, bx, ry, 12.0f, kLbInkDesc, PortTrainerTime(b.nFrame).c_str());
        TextRight(p, bx + 62.0f, ry, 12.0f, kLbInkDesc, N0(b.nSupply).c_str());
        const std::string name = BuildName(b.wType);
        float fNameW = cw - 8.0f - 72.0f;
        if (k > 0 && mine != NULL && mine->wType == b.wType) {
          const long long d = (static_cast<long long>(b.nFrame) - static_cast<long long>(mine->nFrame)) / TRAINER_SECOND;
          if (d >= 3 || d <= -3) {
            const std::string ds = (d > 0 ? "+" : "-") + N0(d > 0 ? d : -d) + "s";
            TextRight(p, bx + cw - 12.0f, ry, 11.5f, d < 0 ? UiRgb(kGood) : UiRgb(kBad), ds.c_str());
            fNameW -= Width(p, 11.5f, ds.c_str()) + 8.0f;
          }
        }
        TextIn(p, bx + 72.0f, ry, fNameW, 12.5f, k == 0 ? kLbInkInfo : UiRgb(0xd0dae6), name);
      }
    }
    return;
  }
  // the rows of the tab, each line's value, the best marked where a row has a better way
  std::vector<Nums> nums;
  for (const TrainerLine *line : lines)
    nums.push_back(NumsOf(*line, nFrom, f));
  int nGraph = g_nGraph;
  if (nGraph < 0 || !RowOnTab(nGraph, g_nSheetTab) || !kRows[nGraph].bSeries) {
    nGraph = -1;
    for (int row = 0; row < ROW_COUNT && nGraph < 0; ++row)
      if (RowOnTab(row, g_nSheetTab) && kRows[row].bSeries)
        nGraph = row;
  }
  for (int row = 0; row < ROW_COUNT; ++row) {
    if (!RowOnTab(row, g_nSheetTab))
      continue;
    const float rh = 36.0f;
    const Rect hit = {r.x + 10.0f, y, r.w - 20.0f, rh - 2.0f};
    if (row == nGraph)
      Fill(p, hit, UiRgb(kThis, 0.08f), 2.0f);
    else if (kRows[row].bSeries && Hot(ACT_GRAPHROW, row))
      Fill(p, hit, UiRgb(0x0a1e32, 0.9f), 2.0f);
    Text(p, r.x + 16.0f, y + 16.0f, 13.5f, row == nGraph ? UiRgb(kThis) : kLbInkDesc, PortText(kRows[row].pszName));
    if (row == ROW_SPENT) {
      float kx = r.x + 16.0f;
      for (int k = 0; k < 4; ++k) {
        Fill(p, {kx, y + 24.0f, 7.0f, 7.0f}, UiRgb(kSpendColours[k]), 1.0f);
        kx += 10.0f + Text(p, kx + 10.0f, y + 31.0f, 10.0f, kLbInkDesc, PortText(kSpends[k])) + 6.0f;
      }
    }
    int nBest = -1;
    if (kRows[row].nGood != 0 && lines.size() > 1)
      for (size_t k = 0; k < nums.size(); ++k) {
        const double v = RowValue(row, nums[k]);
        if (nums[k].bOk && !isnan(v) && (nBest < 0 || v * kRows[row].nGood > RowValue(row, nums[static_cast<size_t>(nBest)]) * kRows[row].nGood))
          nBest = static_cast<int>(k);
      }
    for (size_t k = 0; k < lines.size(); ++k) {
      const float bx = cx0 + cw * k;
      if (row == ROW_SPENT && nums[k].bOk) {
        Text(p, bx, y + 14.0f, 13.5f, k == 0 ? kLbInkInfo : UiRgb(0xd0dae6), RowText(row, nums[k]).c_str(), UI_FACE_BOLD);
        SpendBar(p, bx + 70.0f, y + 6.0f, cw - 90.0f, nums[k]);
        continue;
      }
      const bool bBest = static_cast<int>(k) == nBest;
      Text(p, bx, y + 15.0f, 14.0f, bBest ? UiRgb(kGood) : k == 0 ? kLbInkInfo : UiRgb(0xd0dae6),
                            RowText(row, nums[k]).c_str(), UI_FACE_BOLD);
      const std::string sub = RowSub(row, nums[k]);
      if (!sub.empty())
        TextIn(p, bx, y + 30.0f, cw - 14.0f, 11.0f, kLbInkDesc, sub);
    }
    if (kRows[row].bSeries)
      AddHit(p, hit, ACT_GRAPHROW, row);
    y += rh;
  }
  // a row's course over the game, every line
  if (nGraph >= 0 && r.y + r.h - y > 110.0f) {
    y += 8.0f;
    // {0} the row's name
    Title(p, r.x + 16.0f, y + 10.0f, PortTextF("{0} over the game", {PortText(kRows[nGraph].pszName)}).c_str());
    SheetGraph(p, {r.x + 16.0f, y + 18.0f, r.w - 32.0f, r.y + r.h - y - 26.0f}, nGraph, lines, f, nFrom);
  }
}

// ---- what the player does --------------------------------------------------------------------------------

void OpenTape(bool bPause = true)
{
  if (g_bTape)
    return;
  if (bPause)
    PortTrainerPause();
  g_bTape = true;
  g_bTapeOpening = true;
  g_nMoveAt = SDL_GetTicks();
  g_bAsk = false;
  PortPresentRedraw();
}

// the game goes on (the engine has unpaused it): the Tape out
void CloseTape(void)
{
  if (!g_bTape)
    return;
  g_bTape = false;
  g_bTapeOpening = true;
  g_nMoveAt = SDL_GetTicks();
  g_bAsk = false;
  PortPresentRedraw();
}

void Said(bool bDone, const std::string &said)
{
  if (bDone && !said.empty())
    Toast(said);
}

void Resume(void)
{
  const TrainerLine *me = Cur();
  if (me != NULL && Now() < me->nEnd) {
    // in a line's middle, Space asks the four ways on; asked already, or with the set-up's
    // "always try again", it tries again
    if (!g_bAsk && PortTrainerSetupNow().bAsk) {
      g_bAsk = true;
      return;
    }
    std::string said;
    Said(PortTrainerGoOn(TRAINER_GO_AGAIN, &said), said);
    CloseTape();
    return;
  }
  PortTrainerResume();
  CloseTape();
}

void GoOn(int nChoice)
{
  std::string said;
  const bool bDone = PortTrainerGoOn(nChoice, &said);
  Said(bDone, said);
  if (bDone)
    CloseTape();
}

void Retry(void)
{
  std::string said;
  const bool bDone = PortTrainerRetry(&said);
  Said(bDone, said);
  if (bDone)
    CloseTape();
}

void Branch(void)
{
  std::string said;
  const bool bDone = PortTrainerBranch(&said);
  Said(bDone, said);
  if (bDone && g_bTape) {
    PortTrainerResume();
    CloseTape();
  }
}

void KeepCurrent(void)
{
  std::string said;
  Said(PortTrainerKeep(PortTrainerCurrent(), &said), said);
}

void Point(void)
{
  std::string said;
  Said(PortTrainerSetPoint(&said), said);
}

void Mark(void)
{
  std::string said;
  Said(PortTrainerMarkHere(&said), said);
}

void TakeOver(void)
{
  std::string said;
  const bool bDone = PortTrainerTakeOver(&said);
  Said(bDone, said);
  if (bDone && g_bTape)
    CloseTape();
}

// the game's pace doubled or halved, x1 to x8 (trainer.js's + and -)
void Faster(int nWay)
{
  const int n = PortGameSpeedTimes();
  PortGameSpeedTimesSet(nWay > 0 ? SDL_min(8, n * 2) : SDL_max(1, n / 2));
  // {0} how many times the game's own pace
  Toast(PortTextF("Speed \xc3\x97{0}", {std::to_string(PortGameSpeedTimes())}) + "\n" +
        (PortGameSpeedTimes() > 1
             ? PortTextF("The game's frames {0} times as often", {std::to_string(PortGameSpeedTimes())})
             : std::string(PortText("The game's frames at its own pace"))));
  PortPresentRedraw();
}

void ScrubTo(int nLine, unsigned nFrame)
{
  const TrainerLine *l = PortTrainerLine(nLine);
  if (l == NULL)
    return;
  nFrame = SDL_clamp(nFrame, l->nSplit, l->nEnd);
  g_bAsk = false;
  PortTrainerScrub(nLine, nFrame);
  PortPresentRedraw();
}

void Nudge(int nSeconds)
{
  const TrainerLine *me = Cur();
  if (me == NULL)
    return;
  const long long nTo = static_cast<long long>(Now()) + static_cast<long long>(nSeconds) * TRAINER_SECOND;
  ScrubTo(me->nId, static_cast<unsigned>(SDL_max(0LL, nTo)));
}

void LaneStep(int nWay)
{
  const std::vector<Lane> lanes = Lanes();
  const int nRow = RowOf(TapeFor(g_tape.area), PortTrainerCurrent());
  const int nTo = SDL_clamp(nRow + nWay, 0, static_cast<int>(lanes.size()) - 1);
  if (nRow < 0 || nTo == nRow)
    return;
  const TrainerLine &l = *lanes[static_cast<size_t>(nTo)].pLine;
  ScrubTo(l.nId, SDL_clamp(Now(), l.nSplit, l.nEnd));
}

void JumpMark(int nWay)
{
  const TrainerLine *me = Cur();
  if (me == NULL)
    return;
  std::vector<unsigned> at = {me->nSplit, me->nEnd};
  for (const TrainerMark &m : me->marks)
    at.push_back(m.nFrame);
  for (const TrainerPoint &pt : PortTrainerPoints())
    if (pt.nLine == me->nId)
      at.push_back(pt.nFrame);
  std::sort(at.begin(), at.end());
  const unsigned f = Now();
  if (nWay < 0) {
    for (auto it = at.rbegin(); it != at.rend(); ++it)
      if (*it + 2 < f) {
        ScrubTo(me->nId, *it);
        return;
      }
  } else {
    for (unsigned a : at)
      if (a > f + 2) {
        ScrubTo(me->nId, a);
        return;
      }
  }
}

void CycleVs(void)
{
  const TrainerLine *me = Cur();
  const TrainerLine *now = VsLine();
  std::vector<int> ids;
  for (const Lane &x : Lanes())
    if (me == NULL || x.pLine->nId != me->nId)
      ids.push_back(x.pLine->nId);
  if (ids.empty())
    return;
  size_t i = 0;
  while (i < ids.size() && (now == NULL || ids[i] != now->nId))
    ++i;
  g_nVs = ids[(i + 1) % ids.size()];
}

void TapeAt(float fX, float fY)
{
  const TapeGeom &g = g_tape;
  const float x = fX / g_k, y = fY / g_k;
  const TrainerLine *pLine = Cur();
  if (y >= g.area.y + g.fRuler && !g.lanes.empty()) {
    const int nRow = static_cast<int>((y - g.area.y - g.fRuler) / g.fLane);
    if (nRow >= 0 && nRow < static_cast<int>(g.lanes.size()))
      pLine = g.lanes[static_cast<size_t>(nRow)].pLine;
  }
  if (pLine != NULL)
    ScrubTo(pLine->nId, g.F(x));
}

void Act(Action act, int n)
{
  switch (act) {
    case ACT_RESUME: Resume(); break;
    case ACT_RETRY: Retry(); break;
    case ACT_POINT: Point(); break;
    case ACT_KEEP: KeepCurrent(); break;
    case ACT_BRANCH: Branch(); break;
    case ACT_MARK: Mark(); break;
    case ACT_LEAVE:
      LeaveOpen();
      break;
    case ACT_LEAVEASK:
      LeaveAs(n);
      break;
    case ACT_LEAVENAME:
      g_bNameEdit = true;
      break;
    case ACT_LEAVEBG:
      g_bNameEdit = false;
      break;
    case ACT_LINE: {
      const TrainerLine *l = PortTrainerLine(n);
      if (l != NULL)
        ScrubTo(n, n == PortTrainerCurrent() ? Now() : SDL_clamp(Now(), l->nSplit, l->nEnd));
      break;
    }
    case ACT_KEEPLINE: {
      std::string said;
      Said(PortTrainerKeep(n, &said), said);
      break;
    }
    case ACT_POINTROW: {
      const std::vector<TrainerPoint> &points = PortTrainerPoints();
      if (n >= 0 && n < static_cast<int>(points.size()))
        ScrubTo(points[n].nLine, points[n].nFrame);
      break;
    }
    case ACT_CHOICE:
      GoOn(n == 0 ? TRAINER_GO_AGAIN : n == 1 ? TRAINER_GO_TIMELINE : n == 2 ? TRAINER_GO_REWRITE : TRAINER_GO_WATCH);
      break;
    case ACT_VS: CycleVs(); break;
    case ACT_SCOPE: g_bWholeGame = n == 1; break;
    case ACT_ROW:
      g_nLaneRow = g_nLaneRow == n ? -1 : n;
      g_nGraph = n;
      break;
    case ACT_SHEET: g_bSheet = !g_bSheet; break;
    case ACT_TAB: g_nSheetTab = n; break;
    case ACT_GRAPHROW: g_nGraph = n; break;
    case ACT_LANEFIG: g_nLaneRow = kLaneFigures[n].nRow; break;
    default: break;
  }
  PortPresentRedraw();
}

void Toasts(const Pen &p, const Layout &l, float y)
{
  if (g_toastHead.empty() || SDL_GetTicks() >= g_nToastUntil)
    return;
  const float w = SDL_max(Width(p, 16.0f, g_toastHead.c_str(), UI_FACE_BOLD), Width(p, 13.0f, g_toastLine.c_str())) + 40.0f;
  const Rect r = {(l.fW - w) / 2.0f, y, w, g_toastLine.empty() ? 40.0f : 58.0f};
  Fill(p, r, UiRgb(0x030b16, 0.92f), 3.0f);
  Ring(p, r, kLbHyperionRim, 3.0f, 1.0f);
  Text(p, r.x + 20.0f, r.y + 25.0f, 16.0f, kLbInkInfo, g_toastHead.c_str(), UI_FACE_BOLD);
  if (!g_toastLine.empty())
    Text(p, r.x + 20.0f, r.y + 45.0f, 13.0f, kLbInkDesc, g_toastLine.c_str());
  PortPresentRedraw();  // drawn again until it goes
}

}  // namespace

bool PortTrainerViewShown(void)
{
  return PortTrainerSession() && gwGameMode == 3;
}

static void LayerArea(float fW, float fH, float *pfX, float *pfY, float *pfW, float *pfH)
{
  *pfX = *pfY = 0.0f;
  *pfW = fW;
  *pfH = fH;
  if (!PortTrainerViewShown() || (!g_bTape && !g_bTapeOpening) || fW <= 0.0f || fH <= 0.0f)
    return;
  const Layout l = LayoutFor(fW, fH);
  const float t = FrameIn();
  *pfX = l.frame.x * l.k * t;
  *pfY = l.frame.y * l.k * t;
  *pfW = fW + (l.frame.w * l.k - fW) * t;
  *pfH = fH + (l.frame.h * l.k - fH) * t;
}

static bool LayerDraw(int nWidth, int nHeight, float fX, float fY, float fW, float fH)
{
  g_nButtons = 0;
  g_nCovers = 0;
  g_bDrawn = PortTrainerViewShown();
  if (!g_bDrawn)
    return false;
  const Layout l = LayoutFor(static_cast<float>(nWidth), static_cast<float>(nHeight));
  g_k = l.k;
  const Pen p = {l.k};
  // a saved session, opened: paused at its start with the Tape up
  if (PortTrainerTapeWanted())
    OpenTape(false);
  // the game stopped by something else (1.17's menu, the game's end): no Tape
  if (g_bTape && !PortTrainerHeld())
    CloseTape();
  if (g_bTape || g_bTapeOpening) {
    const float t = FrameIn();
    // round the game's frame: the backdrop, coming in with it
    const UiColor back = UiRgb(0x050d18, 0.97f * t);
    UiFill(0.0f, 0.0f, static_cast<float>(nWidth), fY, back);
    UiFill(0.0f, fY + fH, static_cast<float>(nWidth), static_cast<float>(nHeight) - fY - fH, back);
    UiFill(0.0f, fY, fX, fH, back);
    UiFill(fX + fW, fY, static_cast<float>(nWidth) - fX - fW, fH, back);
    if (g_bTapeOpening)
      PortPresentRedraw();
  }
  if (g_bTape && !g_bTapeOpening) {
    Ring(p, {fX / l.k - 2.0f, fY / l.k - 2.0f, fW / l.k + 4.0f, fH / l.k + 4.0f}, kLbHyperionRim, 2.0f, 2.0f);
    TopBar(p, l);
    Figures(p, l);
    SideLists(p, l);
    Tape(p, l);
    if (g_bSheet)
      Sheet(p, l);
    if (g_bLeaveAsk)
      LeaveAsk(p, l);
    Toasts(p, l, l.top.y + l.top.h + 12.0f);
  } else if (!g_bTape && !g_bTapeOpening) {
    const Rect game = {fX / l.k, fY / l.k, fW / l.k, fH / l.k};
    Strip(p, game);
    Toasts(p, l, game.y + 82.0f);
    // the clock in the strip moves: drawn again each game second
    const unsigned nSecond = Now() / TRAINER_SECOND;
    if (nSecond != g_nLastFrame) {
      g_nLastFrame = nSecond;
      PortPresentRedraw();
    }
  }
  return true;
}

static bool LayerOutside(float fX, float fY)
{
  if (!PortTrainerViewShown() || !g_bDrawn)
    return false;
  if (g_bTape)
    return true;  // the game in its frame is stopped: the Tape takes the mouse
  for (int i = 0; i < g_nCovers; ++i)
    if (g_covers[i].In(fX, fY))
      return true;
  return false;
}

static bool LayerMouse(unsigned uMsg, unsigned long long wParam, long long lParam)
{
  if (!PortTrainerViewShown() || !g_bDrawn)
    return false;
  const float fX = static_cast<float>(static_cast<short>(LOWORD(lParam)));
  const float fY = static_cast<float>(static_cast<short>(HIWORD(lParam)));
  const bool bRelease = uMsg == WM_LBUTTONUP || uMsg == WM_RBUTTONUP || uMsg == WM_MBUTTONUP;
  int nHit = -1;
  for (int i = 0; i < g_nButtons; ++i)
    if (g_buttons[i].r.In(fX, fY))
      nHit = i;
  // the choices lie over the tape: they come first
  for (int i = 0; i < g_nButtons; ++i)
    if (g_buttons[i].act == ACT_CHOICE && g_buttons[i].r.In(fX, fY))
      nHit = i;
  if (nHit != g_nHot) {
    g_nHot = nHit;
    PortPresentRedraw();
  }
  if (nHit >= 0) {
    if (uMsg == WM_LBUTTONDOWN) {
      if (g_buttons[nHit].act == ACT_TAPE)
        TapeAt(fX, fY);
      else
        Act(g_buttons[nHit].act, g_buttons[nHit].n);
    } else if (uMsg == WM_MOUSEMOVE && g_buttons[nHit].act == ACT_TAPE && (wParam & MK_LBUTTON) != 0) {
      TapeAt(fX, fY);
    }
    return !bRelease;
  }
  if (LayerOutside(fX, fY))
    return !bRelease;
  return false;
}

namespace {

bool Key(unsigned vk, bool bAct, bool bRepeat, bool bCtrl, bool bShift)
{
  if (!PortTrainerViewShown())
    return false;
  if (!bAct) {
    // a character: the Save as field's while it has the keys
    if (g_bLeaveAsk && g_bNameEdit) {
      if (vk >= 0x20 && vk < 0x7f && g_leaveName.size() < 60)
        g_leaveName += static_cast<char>(vk);
      PortPresentRedraw();
      return true;
    }
    // a character: the Tape takes them all; playing, the ones Ctrl makes of the trainer's keys
    if (g_bTape)
      return true;
    return bCtrl && (vk < 0x20 || vk == ' ');
  }
  if (vk >= VK_F1 && vk <= VK_F24)
    return false;  // 1.17's and Hyperion's own
  if (!g_bTape) {
    if (!bCtrl || bRepeat)
      return false;
    switch (vk) {
      case VK_SPACE: OpenTape(); return true;
      case 'R': Retry(); return true;
      case 'Q': Point(); return true;
      case 'K': KeepCurrent(); return true;
      case 'B': Branch(); return true;
      case 'M': Mark(); return true;
      case 'T':
        if (PortTrainerState() == TRAINER_WATCH)
          TakeOver();
        return true;
      case VK_OEM_PLUS:
      case VK_ADD: Faster(1); return true;
      case VK_OEM_MINUS:
      case VK_SUBTRACT: Faster(-1); return true;
      default: return false;
    }
  }
  if (g_bTapeOpening && bRepeat)
    return true;
  if (g_bLeaveAsk) {
    if (g_bNameEdit) {
      // the field: Backspace and its repeats, Enter saves, Escape leaves the field
      if (vk == VK_BACK && !g_leaveName.empty())
        g_leaveName.pop_back();
      else if (vk == VK_RETURN && !bRepeat)
        LeaveAs(TRAINER_SAVE_ALL);
      else if (vk == VK_ESCAPE && !bRepeat)
        g_bNameEdit = false;
      PortPresentRedraw();
      return true;
    }
    if (bRepeat)
      return true;
    int nTries = 0;
    for (const TrainerLine &k : PortTrainerLines())
      nTries += k.nKind == TRAINER_ATTEMPT;
    switch (vk) {
      case VK_RETURN:
      case 'S': LeaveAs(TRAINER_SAVE_ALL); break;
      case 'T':
        if (nTries > 0)
          LeaveAs(TRAINER_SAVE_TIMELINES);
        break;
      case 'L': LeaveAs(TRAINER_SAVE_NONE); break;
      case 'C':
      case VK_ESCAPE: LeaveAs(-1); break;
      default: break;
    }
    PortPresentRedraw();
    return true;
  }
  const TrainerLine *me = Cur();
  const bool bMid = me != NULL && Now() < me->nEnd;
  switch (vk) {
    case VK_LEFT: Nudge(bShift ? -10 : -1); break;
    case VK_RIGHT: Nudge(bShift ? 10 : 1); break;
    default:
      if (bRepeat)
        return true;
      switch (vk) {
        case VK_SPACE: Resume(); break;
        case VK_RETURN:
          if (bMid)
            GoOn(TRAINER_GO_AGAIN);
          break;
        case 'K':
          if (bMid)
            GoOn(TRAINER_GO_TIMELINE);
          else
            KeepCurrent();
          break;
        case 'X':
          if (bMid)
            GoOn(TRAINER_GO_REWRITE);
          break;
        case 'W':
          if (bMid)
            GoOn(TRAINER_GO_WATCH);
          break;
        case 'T': TakeOver(); break;
        case 'R': Retry(); break;
        case 'Q': Point(); break;
        case 'B': Branch(); break;
        case 'M': Mark(); break;
        case 'V': CycleVs(); break;
        case VK_OEM_PLUS:
        case VK_ADD: Faster(1); break;
        case VK_OEM_MINUS:
        case VK_SUBTRACT: Faster(-1); break;
        case 'S': g_bSheet = !g_bSheet; break;
        case 'G': {
          int i = 0;
          while (i < 6 && kLaneFigures[i].nRow != g_nLaneRow)
            ++i;
          g_nLaneRow = kLaneFigures[(i + 1) % 6].nRow;
          break;
        }
        case VK_UP: LaneStep(-1); break;
        case VK_DOWN: LaneStep(1); break;
        case VK_OEM_4: JumpMark(-1); break;  // [
        case VK_OEM_6: JumpMark(1); break;   // ]
        case 'L': LeaveOpen(); break;
        case VK_ESCAPE:
          if (g_bSheet)
            g_bSheet = false;
          else if (g_bAsk)
            g_bAsk = false;
          else
            LeaveOpen();
          break;
        default: break;
      }
  }
  PortPresentRedraw();
  return true;
}

}  // namespace

static bool LayerKey(unsigned vk, bool bAct, bool bRepeat)
{
  return Key(vk, bAct, bRepeat, GetKeyState(VK_CONTROL) < 0, GetKeyState(VK_SHIFT) < 0);
}

bool PortTrainerViewKeyAs(unsigned vk, bool bCtrl, bool bShift)
{
  return Key(vk, true, false, bCtrl, bShift);
}

bool PortTrainerViewTape(void)
{
  return g_bTape;
}

// The trainer's screen as a layer over the game (port_overlay.h).
const PortOverlayLayer kLayer = {
    "trainer", PORT_OVERLAY_TRAINER, 0,
    PortTrainerViewShown, LayerArea, LayerDraw, NULL, LayerMouse, LayerKey, LayerOutside};
PORT_OVERLAY_LAYER(kLayer);
