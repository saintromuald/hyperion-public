// port_hudstats.cpp -- the players' stats a viewer calls up (port_hudstats.h), as the design
// center's page draws them (hud.js's statsSide, statsPopup and statsChart; hud.html's .pstats).
//
// No clock of the system's in the game's path: the figures are taken on game frames, and F1's hold
// is timed with SDL's clock only after a key, which no replay of the suite presses
// (port_hudstate.cpp's note).
#include <windows.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <algorithm>
#include <array>
#include <string>
#include <vector>
#include <SDL3/SDL.h>
#include "CUnit.h"
#include "oc_data.h"
#include "port_hudstats.h"
#include "port_hudstate.h"
#include "port_hud.h"                    // PortHudStatsPlaced
#include "../../h/CUnitMisc.h"           // get_unit_name_from_type
#include "../../h/count.h"               // count_get_supplies_used, count_get_supplies_available
#include "../../h/grp.h"                 // FRAME
#include "../../h/net_cmds.h"            // PlayerData
#include "../../h/playerlimits.h"        // PlayerLobbySlots, PlayerIsLobby
#include "../../h/playertables.h"
#include "../../h/Storm.h"               // SFileLoadFile: the command icons
#include "../../gen/globals117.h"
#include "../data/port_stormapi.h"       // SFileUnloadFile
#include "../front/port_frontkit.h"      // the interface's colours, for a view in a place of its own
#include "../platform/port_globals117.h"  // the minerals and gas gathered
#include "../platform/port_winmsg.h"     // WinMsgKeyState
#include "../render/port_maptext.h"     // MapTextUtf8
#include "../render/port_hwcursor.h"     // PortGrpFrameDecode
#include "../render/port_overlay.h"
#include "../render/port_present.h"      // PortPresentPaletteRGB, PortPresentRedraw

extern BYTE gbInMsgMode;  // 0x6BE658 (textbox.cpp): a message is being typed

namespace {

const double kRowGap = 3, kTeamGap = 8;  // the side view's rows and teams apart
const double kSideRowH[2] = {24, 52}, kPopupRowH[2] = {22, 34};
const double kHeaderH = 44, kHeadH = 27;  // the popup's header and its column headings
const double kChartLeast = 150;           // the room the rows leave for the graph, at least
const unsigned SAMPLE_FRAMES = 48;        // two seconds of game time, 42 ms a frame
const unsigned MINUTE_FRAMES = 1429;      // a minute
const unsigned FIRST_FRAMES = 240;        // ten seconds, before which a rate means little
const size_t SAMPLES_MOST = 2400;         // past it every other is dropped, the step doubled
const Uint64 HOLD_MS = 300;               // F1 held this long shows the popup only while it is down

// ---------------------------------------------------------------------------
// The views.

bool g_abOn[HUD_STATS_VIEWS];
int g_nPage;
bool g_bTouched;  // set since the game started (PortHudStatsTouched)
bool g_bHoldWatch, g_bHeld;  // F1 opened the popup and is watched; held past HOLD_MS
Uint64 g_nHoldFrom;
double g_fScroll;            // the popup's rows scrolled, in units
float g_mx = -1, g_my = -1;  // the pointer, in the window's pixels
// where each view was drawn last, in the target's pixels, and whether in a place of its own
struct Drawn {
  HudStatsAt at;
  double rowH[2] = {0, 0};
  Uint64 nAt = 0;
  bool bOwn = false;
};
Drawn g_drawn[HUD_STATS_VIEWS];
double g_fScrollMost;  // the popup's rows past its body, as last laid out
// a view in a place of its own: what the pointer is over, for its tooltip
int g_nOwnTip = -1;
std::string g_ownTip;

bool Seated(int s) {
  return PlayerIsLobby(s) && (gPlayerData[s].nType == 1 || gPlayerData[s].nType == 2);
}

// ---------------------------------------------------------------------------
// The figures over the game: the minerals and gas mined at frames of the last minute and more, for
// the income, and the graph's samples. Each from the game's frames only.

struct Mined {
  unsigned nFrame;
  int an[PORT_HUD_PLAYERS][2];
};
const int MINED_KEPT = 40;  // past a minute at SAMPLE_FRAMES apart
Mined g_mined[MINED_KEPT];
int g_nMined, g_nMinedNext;
unsigned g_nFrom;  // the game's first frame

// a sample: the frame, and per seated slot its supply, income, army value and value lost
struct Sample {
  unsigned nFrame;
  std::vector<std::array<int, 5>> v;  // slot, supply, income a minute, army value, value lost
};
std::vector<Sample> g_samples;
unsigned g_nStep = SAMPLE_FRAMES;

// A unit of the army: living and made, not a building, a worker, an Overlord, a carrier's
// Interceptor, a Reaver's Scarab, a mine, a missile, a Larva, an egg or a cocoon, nor a
// hallucination.
bool IsArmyUnit(CUnit *p) {
  const int t = p->wType;
  if (t < 0 || t >= 228 || p->IsDead() || (p->dwFlags & 1) == 0 || (p->dwFlags & 0x40000000) != 0)
    return false;
  if ((gulUnitTypeFlags[t] & (0x1 | 0x8 | 0x10 | 0x20000000)) != 0)  // building, worker, subunit, invincible
    return false;
  switch (t) {
    case 0x0D: case 0x0E: case 0x23: case 0x24: case 0x2A: case 0x3B: case 0x55: case 0x56: case 0x61:
      return false;
  }
  return true;
}

int Cost(int t) { return static_cast<int>(guwUnitMineralsCost[t]) + static_cast<int>(guwUnitGasesCost[t]); }

// The units a slot lost and killed, and what they cost; a Scarab, a mine or a missile spent is none.
void Losses(int s, int *pnLost, int *pnLostValue, int *pnKilled, int *pnKilledValue) {
  *pnLost = *pnLostValue = *pnKilled = *pnKilledValue = 0;
  for (int t = 0; t < 228; ++t) {
    if (t == 0x0D || t == 0x0E || t == 0x55)
      continue;
    const int nLost = static_cast<int>(glCounts_g[3][t][s]), nKilled = static_cast<int>(glCounts_g[2][t][s]);
    *pnLost += nLost;
    *pnLostValue += nLost * Cost(t);
    *pnKilled += nKilled;
    *pnKilledValue += nKilled * Cost(t);
  }
}

// What a slot mined over the last minute of game time, from the latest figures a minute or more
// old, or from the game's start, scaled to a minute; none for the first ten seconds.
void Income(int s, unsigned nNow, int *pnMinerals, int *pnGas) {
  *pnMinerals = *pnGas = 0;
  unsigned nAt = g_nFrom;
  int m0 = 0, g0 = 0;
  for (int i = 0; i < g_nMined; ++i) {
    const Mined &m = g_mined[(g_nMinedNext - 1 - i + MINED_KEPT) % MINED_KEPT];
    if (m.nFrame <= nNow && nNow - m.nFrame >= MINUTE_FRAMES) {
      nAt = m.nFrame, m0 = m.an[s][0], g0 = m.an[s][1];
      break;
    }
  }
  if (nNow < nAt || nNow - nAt < FIRST_FRAMES)
    return;
  const unsigned nSpan = nNow - nAt;
  *pnMinerals = static_cast<int>((static_cast<long long>(gPlayerMineralsGathered_g[s]) - m0) * MINUTE_FRAMES / nSpan);
  *pnGas = static_cast<int>((static_cast<long long>(gPlayerGasGathered_g[s]) - g0) * MINUTE_FRAMES / nSpan);
}

// The army's value of each slot: one pass over the units.
void ArmyValues(int anValue[PORT_HUD_PLAYERS]) {
  memset(anValue, 0, sizeof(int) * PORT_HUD_PLAYERS);
  for (CUnit *p = CUnit::GetUsedList(); p != NULL; p = p->pNext)
    if (p->bPlayer < PORT_HUD_PLAYERS && IsArmyUnit(p))
      anValue[p->bPlayer] += Cost(p->wType);
}

}  // namespace

void PortHudStatsReset(void) {
  // the popup closes as any game starts; the side view stays up from replay to replay, and goes when
  // a game the player plays starts
  g_abOn[HUD_STATS_POPUP] = false;
  if (!gPlayback)
    g_abOn[HUD_STATS_SIDE] = false;
  g_bTouched = false;
  g_bHoldWatch = g_bHeld = false;
  g_fScroll = 0;
  g_nFrom = oc_dwReplayFrame;
  g_nMined = g_nMinedNext = 0;
  g_samples.clear();
  g_nStep = SAMPLE_FRAMES;
}

void PortHudStatsFrame(void) {
  if (gwGameMode != 3)
    return;
  const unsigned nNow = oc_dwReplayFrame;
  // a replay taken back to an earlier frame: what was taken past it goes
  while (!g_samples.empty() && g_samples.back().nFrame > nNow)
    g_samples.pop_back();
  while (g_nMined > 0 && g_mined[(g_nMinedNext - 1 + MINED_KEPT) % MINED_KEPT].nFrame > nNow) {
    g_nMinedNext = (g_nMinedNext - 1 + MINED_KEPT) % MINED_KEPT;
    --g_nMined;
  }
  if (nNow < g_nFrom)
    g_nFrom = nNow;
  const int nSlots = PlayerLobbySlots() < PORT_HUD_PLAYERS ? PlayerLobbySlots() : PORT_HUD_PLAYERS;
  if (g_nMined == 0 || nNow - g_mined[(g_nMinedNext - 1 + MINED_KEPT) % MINED_KEPT].nFrame >= SAMPLE_FRAMES) {
    Mined &m = g_mined[g_nMinedNext];
    m.nFrame = nNow;
    for (int s = 0; s < nSlots; ++s) {
      m.an[s][0] = gPlayerMineralsGathered_g[s];
      m.an[s][1] = gPlayerGasGathered_g[s];
    }
    g_nMinedNext = (g_nMinedNext + 1) % MINED_KEPT;
    g_nMined = g_nMined < MINED_KEPT ? g_nMined + 1 : MINED_KEPT;
  }
  if (!g_samples.empty() && nNow - g_samples.back().nFrame < g_nStep)
    return;
  // the graph's figures: a viewer's only, as nothing else reads them
  if (!PortHudStatsAllowed())
    return;
  int anValue[PORT_HUD_PLAYERS];
  ArmyValues(anValue);
  Sample smp;
  smp.nFrame = nNow;
  for (int s = 0; s < nSlots; ++s) {
    if (!Seated(s))
      continue;
    const int nRace = gPlayerData[s].nRace <= 2 ? gPlayerData[s].nRace : 1;
    int m, g, nLost, nLostValue, nKilled, nKilledValue;
    Income(s, nNow, &m, &g);
    Losses(s, &nLost, &nLostValue, &nKilled, &nKilledValue);
    const int nSupply = (count_get_supplies_used(static_cast<BYTE>(nRace), static_cast<BYTE>(s)) + 1) / 2;
    smp.v.push_back({s, nSupply, m + g, anValue[s], nLostValue});
  }
  g_samples.push_back(std::move(smp));
  if (g_samples.size() > SAMPLES_MOST) {
    std::vector<Sample> kept;
    for (size_t i = 0; i < g_samples.size(); i += 2)
      kept.push_back(std::move(g_samples[i]));
    g_samples.swap(kept);
    g_nStep *= 2;
  }
}

void PortHudStatsFigures(PortHudState *s) {
  s->bStatsSide = s->bStatsPopup = false;
  s->nStatsPage = g_nPage;
  if (!(s->bReplay || s->bObserver))
    return;
  s->bStatsSide = g_abOn[HUD_STATS_SIDE];
  s->bStatsPopup = g_abOn[HUD_STATS_POPUP];
  if (!s->bStatsSide && !s->bStatsPopup)
    return;  // the figures below are the views' only
  // the army by slot: its supply in halves, its value, its units by type
  static int s_anHalves[PORT_HUD_PLAYERS], s_anValue[PORT_HUD_PLAYERS];
  static unsigned short s_aanCount[PORT_HUD_PLAYERS][228];
  memset(s_anHalves, 0, sizeof s_anHalves);
  memset(s_anValue, 0, sizeof s_anValue);
  for (int i = 0; i < s->nPlayers; ++i)
    memset(s_aanCount[s->aPlayers[i].nSlot], 0, sizeof s_aanCount[0]);
  for (CUnit *p = CUnit::GetUsedList(); p != NULL; p = p->pNext) {
    if (p->bPlayer >= PORT_HUD_PLAYERS || !IsArmyUnit(p))
      continue;
    s_anHalves[p->bPlayer] += gubUnitFoodUsed[p->wType];
    s_anValue[p->bPlayer] += Cost(p->wType);
    unsigned short &n = s_aanCount[p->bPlayer][p->wType];
    n = n < 0xFFFF ? static_cast<unsigned short>(n + 1) : n;
  }
  for (int i = 0; i < s->nPlayers; ++i) {
    PortHudPlayer &p = s->aPlayers[i];
    const int slot = p.nSlot;
    p.nArmySupply = (s_anHalves[slot] + 1) / 2;
    p.nArmyValue = s_anValue[slot];
    Income(slot, s->nFrame, &p.nIncomeMinerals, &p.nIncomeGas);
    Losses(slot, &p.nLost, &p.nLostValue, &p.nKilled, &p.nKilledValue);
    // the types, the most first
    std::vector<std::pair<int, int>> types;
    for (int t = 0; t < 228; ++t)
      if (s_aanCount[slot][t] != 0)
        types.push_back({-static_cast<int>(s_aanCount[slot][t]), t});
    std::sort(types.begin(), types.end());
    p.nUnitTypes = 0;
    for (const auto &e : types) {
      if (p.nUnitTypes == PORT_HUD_UNIT_TYPES)
        break;
      p.awUnitType[p.nUnitTypes] = static_cast<unsigned short>(e.second);
      p.awUnitCount[p.nUnitTypes++] = static_cast<unsigned short>(-e.first);
    }
  }
}

bool PortHudStatsAllowed(void) {
  // every way of watching a game is a replay: a LAN room's match and the lab's games too
  return gwGameMode == 3 && gPlayback != 0;
}

bool PortHudStatsOn(int nView) {
  return nView >= 0 && nView < HUD_STATS_VIEWS && g_abOn[nView] && PortHudStatsAllowed();
}

void PortHudStatsSet(int nView, bool bOn) {
  if (nView >= 0 && nView < HUD_STATS_VIEWS)
    g_bTouched = true;
  if (nView < 0 || nView >= HUD_STATS_VIEWS || g_abOn[nView] == bOn || (bOn && !PortHudStatsAllowed()))
    return;
  g_abOn[nView] = bOn;
  // a view in a place of its own wears the interface's theme, which a game started without the
  // menus has not read
  if (bOn && !PortHudStatsPlaced(nView))
    FrontKit::ThemeApply();
  if (nView == HUD_STATS_POPUP) {
    g_bHoldWatch = g_bHeld = false;
    g_fScroll = 0;
  }
  PortHudStatsEvent(bOn, nView);
  PortPresentRedraw();
}

bool PortHudStatsTouched(void) { return g_bTouched; }

void PortHudStatsTurn(int nPage) {
  g_bTouched = true;
  nPage = ((nPage % HUD_STATS_PAGES) + HUD_STATS_PAGES) % HUD_STATS_PAGES;
  if (nPage == g_nPage)
    return;
  g_nPage = nPage;
  g_fScroll = 0;
  PortPresentRedraw();
}

void HudStatsSlot(const PortJson &def, int *pnView, double rowH[2], double *pBacking) {
  *pnView = def["view"].Str() == "popup" ? HUD_STATS_POPUP : HUD_STATS_SIDE;
  const double *pDef = *pnView == HUD_STATS_POPUP ? kPopupRowH : kSideRowH;
  rowH[0] = pDef[0], rowH[1] = pDef[1];
  if (def["rowH"].size() >= 2) {
    rowH[0] = def["rowH"].At(0).Num(pDef[0]);
    rowH[1] = (std::max)(rowH[0], def["rowH"].At(1).Num(pDef[1]));
  }
  *pBacking = SDL_clamp(def["backing"].Num(0.38), 0.0, 1.0);
}

namespace {

// ---------------------------------------------------------------------------
// The layout of a view, in its box's units: what the drawing and the mouse share.

// The players by team, in the order the teams first come; in a game without teams each player alone.
std::vector<std::vector<int>> TeamsOf(const PortHudState &st) {
  std::vector<std::vector<int>> teams;
  std::vector<int> keys;
  for (int i = 0; i < st.nPlayers; ++i) {
    const int key = st.aPlayers[i].nTeam > 0 ? st.aPlayers[i].nTeam : 1000 + i;
    const size_t k = std::find(keys.begin(), keys.end(), key) - keys.begin();
    if (k == keys.size()) {
      keys.push_back(key);
      teams.push_back(std::vector<int>());
    }
    teams[k].push_back(i);
  }
  return teams;
}

// The side view: a row a player from the top, the teams a little apart, the rows sharing the
// box's height between rowH's least and most; past those that fit at the least, the last row is
// "+N more".
struct SideRow {
  int nPlayer;  // -1 the "+N more" row
  double y;
};
struct Side {
  double rh = 0;
  bool bTwo = false;
  int nMore = 0;
  std::vector<SideRow> rows;
};

Side SideOf(double h, const double rowH[2], const PortHudState &st) {
  Side sd;
  const std::vector<std::vector<int>> teams = TeamsOf(st);
  const int n = st.nPlayers;
  if (n <= 0)
    return sd;
  const double free = h - (teams.size() - 1) * kTeamGap;
  double rh = SDL_clamp((free - (n - 1) * kRowGap) / n, rowH[0], rowH[1]);
  const int fit = (std::max)(1, static_cast<int>(floor((free + kRowGap) / (rowH[0] + kRowGap))));
  std::vector<std::vector<int>> shown = teams;
  if (n > fit) {
    int left = fit - 1;
    shown.clear();
    for (const std::vector<int> &t : teams) {
      std::vector<int> part(t.begin(), t.begin() + (std::min)(static_cast<int>(t.size()), (std::max)(0, left)));
      left -= static_cast<int>(part.size());
      if (!part.empty())
        shown.push_back(part);
    }
    sd.nMore = n - (fit - 1);
    rh = rowH[0];
  }
  sd.rh = rh;
  sd.bTwo = rh >= 40;
  double y = 0;
  for (size_t k = 0; k < shown.size(); ++k) {
    if (k > 0)
      y += kTeamGap;
    for (size_t j = 0; j < shown[k].size(); ++j) {
      if (j > 0)
        y += kRowGap;
      sd.rows.push_back({shown[k][j], y});
      y += rh;
    }
  }
  if (sd.nMore > 0)
    sd.rows.push_back({-1, shown.empty() ? 0 : y + kTeamGap});
  return sd;
}

// The popup's columns: a figure, its heading and how it is drawn.
enum { HOW_NUM, HOW_BAR, HOW_UNITS, HOW_EYE };
enum {
  K_MINERALS, K_GAS, K_SUPPLY, K_WORKERS, K_ARMY, K_APM, K_EYE, K_INC_M, K_INC_G, K_INC_T, K_VALUE, K_UNITS,
  K_LOST_N, K_LOST, K_KILLED_N, K_KILLED
};
struct ColDef {
  int nKey;
  const char *pszHead;
  int nHow;
};
const ColDef kOverview[] = {{K_MINERALS, PT_("Minerals"), HOW_NUM}, {K_GAS, PT_("Gas"), HOW_NUM},
                            {K_SUPPLY, PT_("Supply"), HOW_NUM},     {K_WORKERS, PT_("Workers"), HOW_NUM},
                            {K_ARMY, PT_("Army"), HOW_NUM},         {K_APM, "APM", HOW_NUM},
                            {K_EYE, PT_("Vision"), HOW_EYE}};
const ColDef kIncome[] = {{K_INC_M, PT_("Minerals a minute"), HOW_BAR}, {K_INC_G, PT_("Gas a minute"), HOW_BAR},
                          {K_WORKERS, PT_("Workers"), HOW_NUM},         {K_INC_T, PT_("In all a minute"), HOW_NUM}};
const ColDef kArmy[] = {{K_ARMY, PT_("Army supply"), HOW_NUM},
                        {K_VALUE, PT_("Army value"), HOW_BAR},
                        {K_UNITS, PT_("Units"), HOW_UNITS}};
const ColDef kLost[] = {{K_LOST_N, PT_("Units lost"), HOW_NUM}, {K_LOST, PT_("Value lost"), HOW_BAR},
                        {K_KILLED_N, PT_("Units killed"), HOW_NUM}, {K_KILLED, PT_("Value killed"), HOW_BAR}};
const char *const kPageNames[HUD_STATS_PAGES] = {PT_("Overview"), PT_("Income"), PT_("Army"), PT_("Units lost")};
const char *const kChartTitles[HUD_STATS_PAGES] = {PT_("Supply over the game"),
                                                   PT_("Income a minute over the game"),
                                                   PT_("Army value over the game"), PT_("Value lost over the game")};

const ColDef *PageCols(int nPage, int *pnCols) {
  switch (nPage) {
    case HUD_STATS_INCOME:
      *pnCols = 4;
      return kIncome;
    case HUD_STATS_ARMY:
      *pnCols = 3;
      return kArmy;
    case HUD_STATS_LOST:
      *pnCols = 4;
      return kLost;
  }
  *pnCols = 7;
  return kOverview;
}

double Value(const PortHudPlayer &p, int nKey) {
  switch (nKey) {
    case K_MINERALS: return p.nMinerals;
    case K_GAS: return p.nGas;
    case K_SUPPLY: return p.nSupplyUsed;
    case K_WORKERS: return p.nWorkers;
    case K_ARMY: return p.nArmySupply;
    case K_APM: return p.nApm;
    case K_INC_M: return p.nIncomeMinerals;
    case K_INC_G: return p.nIncomeGas;
    case K_INC_T: return p.nIncomeMinerals + p.nIncomeGas;
    case K_VALUE: return p.nArmyValue;
    case K_LOST_N: return p.nLost;
    case K_LOST: return p.nLostValue;
    case K_KILLED_N: return p.nKilled;
    case K_KILLED: return p.nKilledValue;
  }
  return 0;
}

// A number as the page's toLocaleString writes it, thousands apart with commas.
std::string Fmt(double d) {
  long long n = static_cast<long long>(floor(d + 0.5));
  const bool bNeg = n < 0;
  if (bNeg)
    n = -n;
  std::string s = std::to_string(n);
  const std::string mark = PortTextC("thousands", ",");
  for (int i = static_cast<int>(s.size()) - 3; i > 0; i -= 3)
    s.insert(static_cast<size_t>(i), mark);
  return bNeg ? "-" + s : s;
}

std::string Upper(std::string s) {
  return UiUpper(s.c_str());
}

// Text widths in units: the font's at a size in units are the units' own.
double Width(UiFace face, double size, double spacing, const std::string &s) {
  return s.empty() ? 0.0 : UiTextWidth(face, static_cast<float>(size), static_cast<float>(spacing), s.c_str());
}

struct Col {
  ColDef def;
  double x = 0, w = 0;
};
struct Line {
  int nPlayer;  // -1 a team's heading
  int nTeam;    // its team's index
  double y, h;  // in the body, before its scroll
};
struct Popup {
  double rh = 0, bodyY = kHeaderH + kHeadH, bodyH = 0, contentH = 0, chartY = 0, chartH = 0;
  double plX = 14, plW = 0;  // the player's column
  std::vector<Col> cols;
  double tabX[HUD_STATS_PAGES] = {}, tabW[HUD_STATS_PAGES] = {};
  double closeX = 0;  // the close button, 32 square at (closeX, 4)
  double sumX = 0, keysX = 0;
  bool bKeys = false;
  bool bHeaded = false;  // the teams apart, each of the lobby's teams under its heading
  int nTeams = 0;        // the lobby's teams, not counting players without one
  std::vector<std::vector<int>> teams;
  std::vector<Line> lines;
};

const char *const kKeysHint = PT_("Tab next page \xC2\xB7 Shift+F1 side view \xC2\xB7 F1 close");

std::string SumText(const PortHudState &st, const Popup &pp) {
  std::string s = PortTextNF("{0} player", "{0} players", st.nPlayers, {std::to_string(st.nPlayers)});
  if (pp.nTeams > 1)
    s += " \xC2\xB7 " + PortTextNF("{0} team", "{0} teams", pp.nTeams, {std::to_string(pp.nTeams)});
  return s;
}

Popup PopupOf(double w, double h, const double rowH[2], const PortHudState &st, int nPage) {
  Popup pp;
  pp.teams = TeamsOf(st);
  // a player without a team is a group alone, with no heading
  bool bMany = false;
  for (const std::vector<int> &t : pp.teams) {
    bMany = bMany || t.size() > 1;
    pp.nTeams += st.aPlayers[t[0]].nTeam > 0 ? 1 : 0;
  }
  pp.bHeaded = pp.teams.size() > 1 && bMany;
  auto headed = [&](size_t t) { return pp.bHeaded && st.aPlayers[pp.teams[t][0]].nTeam > 0; };
  // the header: the title, the count, the pages' tabs, the keys at the right before the close
  const double titleW = Width(UI_FACE_BOLD, 17, 17 * 0.18, PortText("PLAYERS"));
  pp.sumX = 14 + titleW + 14;
  double x = pp.sumX + Width(UI_FACE_TEXT, 12, 12 * 0.08, SumText(st, pp)) + 14 + 8;
  for (int i = 0; i < HUD_STATS_PAGES; ++i) {
    pp.tabX[i] = x;
    pp.tabW[i] = Width(UI_FACE_TEXT, 12, 12 * 0.14, Upper(PortText(kPageNames[i]))) + 24;
    x += pp.tabW[i] + 2;
  }
  pp.closeX = w - 4 - 32;
  const double keysW = Width(UI_FACE_TEXT, 11, 11 * 0.06, PortText(kKeysHint));
  pp.keysX = pp.closeX - 14 - keysW;
  pp.bKeys = pp.keysX >= x + 12;
  // the columns: the player's, then the page's, as a grid of minmax tracks 16 apart
  int nCols = 0;
  const ColDef *pDefs = PageCols(nPage, &nCols);
  struct Track {
    double least, fr, w;
    bool bFixed;
  };
  std::vector<Track> tracks;
  tracks.push_back({150, 1.5, 0, false});
  for (int i = 0; i < nCols; ++i) {
    switch (pDefs[i].nHow) {
      case HOW_BAR: tracks.push_back({150, 1.6, 0, false}); break;
      case HOW_UNITS: tracks.push_back({0, 3.4, 0, false}); break;
      case HOW_EYE: tracks.push_back({56, 0, 56, true}); break;
      default: tracks.push_back({64, 0.7, 0, false}); break;
    }
  }
  const double inner = w - 28 - 16.0 * (tracks.size() - 1);
  // the fractions shared out, a track whose share is under its least kept at that and the rest shared again
  for (bool bAgain = true; bAgain;) {
    bAgain = false;
    double free = inner, fr = 0;
    for (const Track &t : tracks) {
      if (t.bFixed)
        free -= t.w;
      else
        fr += t.fr;
    }
    const double unit = fr > 0 ? (std::max)(0.0, free) / fr : 0;
    for (Track &t : tracks)
      if (!t.bFixed && unit * t.fr < t.least) {
        t.bFixed = true;
        t.w = t.least;
        bAgain = true;
      }
    if (!bAgain)
      for (Track &t : tracks)
        if (!t.bFixed)
          t.w = unit * t.fr;
  }
  double cx = 14;
  pp.plX = cx;
  pp.plW = tracks[0].w;
  cx += tracks[0].w + 16;
  for (int i = 0; i < nCols; ++i) {
    Col c;
    c.def = pDefs[i];
    c.x = cx;
    c.w = tracks[i + 1].w;
    pp.cols.push_back(c);
    cx += c.w + 16;
  }
  // the rows: at their most height when that leaves the graph its room, else down to their least,
  // and past that the body scrolls
  const double avail = h - pp.bodyY;
  int Lt = 0;  // the headings
  for (size_t t = 0; t < pp.teams.size(); ++t)
    Lt += headed(t) ? 1 : 0;
  const int Lp = st.nPlayers, Lg = pp.bHeaded ? static_cast<int>(pp.teams.size()) - 1 : 0;  // the gaps
  auto tableH = [&](double r) { return Lp * r + Lt * 0.8 * r + Lg * 8.0 + 8.0; };
  if (tableH(rowH[1]) + kChartLeast <= avail) {
    pp.rh = rowH[1];
    pp.chartH = floor((avail - tableH(pp.rh) - 6) / 10) * 10;
  } else {
    pp.rh = SDL_clamp((avail - 8 - Lg * 8.0) / (Lp + 0.8 * Lt), rowH[0], rowH[1]);
  }
  pp.bodyH = avail - pp.chartH;
  pp.chartY = h - pp.chartH;
  double y = 0;
  for (size_t t = 0; t < pp.teams.size(); ++t) {
    if (pp.bHeaded && t > 0)
      y += 8;
    if (headed(t)) {
      pp.lines.push_back({-1, static_cast<int>(t), y, pp.rh * 0.8});
      y += pp.rh * 0.8;
    }
    for (int i : pp.teams[t]) {
      pp.lines.push_back({i, static_cast<int>(t), y, pp.rh});
      y += pp.rh;
    }
  }
  pp.contentH = y + 6;
  return pp;
}

double ScrollOf(const Popup &pp) {
  g_fScrollMost = (std::max)(0.0, pp.contentH - pp.bodyH);
  g_fScroll = SDL_clamp(g_fScroll, 0.0, g_fScrollMost);
  return g_fScroll;
}

// What a point of a view is on.
enum { ON_NONE, ON_MORE, ON_TAB, ON_CLOSE, ON_EYE, ON_NOEYE, ON_BODY };
struct On {
  int nWhat = ON_NONE;
  int nArg = 0;         // the tab's page, the eye's player
  double q[4] = {};     // its box
};

On OnSide(double h, const double rowH[2], double lx, double ly, const PortHudState &st, double w) {
  On on;
  const Side sd = SideOf(h, rowH, st);
  for (const SideRow &r : sd.rows)
    if (r.nPlayer < 0 && ly >= r.y && ly < r.y + sd.rh && lx >= 0 && lx < w) {
      on.nWhat = ON_MORE;
      on.q[0] = 0, on.q[1] = r.y, on.q[2] = w, on.q[3] = sd.rh;
    }
  return on;
}

On OnPopup(double w, double h, const double rowH[2], double lx, double ly, const PortHudState &st) {
  On on;
  if (lx < 0 || ly < 0 || lx > w || ly > h)
    return on;
  on.nWhat = ON_BODY;
  const Popup pp = PopupOf(w, h, rowH, st, g_nPage);
  if (lx >= pp.closeX && lx < pp.closeX + 32 && ly >= 4 && ly < 36) {
    on.nWhat = ON_CLOSE;
    on.q[0] = pp.closeX, on.q[1] = 4, on.q[2] = 32, on.q[3] = 32;
    return on;
  }
  for (int i = 0; i < HUD_STATS_PAGES; ++i)
    if (lx >= pp.tabX[i] && lx < pp.tabX[i] + pp.tabW[i] && ly >= 6 && ly < 34) {
      on.nWhat = ON_TAB;
      on.nArg = i;
      on.q[0] = pp.tabX[i], on.q[1] = 6, on.q[2] = pp.tabW[i], on.q[3] = 28;
      return on;
    }
  const Col *pEye = NULL;
  for (const Col &c : pp.cols)
    if (c.def.nHow == HOW_EYE)
      pEye = &c;
  if (pEye == NULL || ly < pp.bodyY || ly >= pp.bodyY + pp.bodyH || lx < pEye->x || lx >= pEye->x + pEye->w)
    return on;
  const double scroll = (std::min)(g_fScroll, (std::max)(0.0, pp.contentH - pp.bodyH));
  for (const Line &ln : pp.lines) {
    const double y = pp.bodyY + ln.y - scroll;
    if (ln.nPlayer < 0 || ly < y || ly >= y + ln.h)
      continue;
    const PortHudPlayer &p = st.aPlayers[ln.nPlayer];
    const double e = (std::min)(pp.rh, 30.0) * 0.85;
    on.nWhat = p.bEye ? ON_EYE : ON_NOEYE;
    on.nArg = ln.nPlayer;
    on.q[0] = pEye->x + (pEye->w - e) / 2, on.q[1] = y + (ln.h - e) / 2, on.q[2] = e, on.q[3] = e;
    if (lx < on.q[0] || lx >= on.q[0] + e)
      on.nWhat = ON_BODY;
    return on;
  }
  return on;
}

void Act(const On &on, const PortHudState &st) {
  const char *pszWhy = NULL;
  switch (on.nWhat) {
    case ON_MORE:
      PortHudStatsSet(HUD_STATS_POPUP, true);
      break;
    case ON_TAB:
      PortHudStatsTurn(on.nArg);
      break;
    case ON_CLOSE:
      PortHudStatsSet(HUD_STATS_POPUP, false);
      break;
    case ON_EYE:
      PortHudPressEye(st.aPlayers[on.nArg].nSlot, &pszWhy);
      break;
  }
}

// A point's tooltip, an id, -1 none, and its words and key (the design center's titles).
int TipOf(const On &on, const PortHudState &st, std::string *pTitle, std::string *pKey) {
  pKey->clear();
  switch (on.nWhat) {
    case ON_MORE:
      *pTitle = PortText("Every player, in the stats popup");
      *pKey = "F1";
      return 400;
    case ON_CLOSE:
      *pTitle = PortText("Close");
      *pKey = "F1";
      return 401;
    case ON_NOEYE:
      *pTitle = PortText("Vision switches for the first eight players only");
      return 402;
    case ON_EYE: {
      const PortHudPlayer &p = st.aPlayers[on.nArg];
      // {0} the player's name
      *pTitle = p.bSeen ? PortTextF("Hide {0}\xE2\x80\x99s vision", {p.szName})
                        : PortTextF("Show {0}\xE2\x80\x99s vision", {p.szName});
      return 410 + p.nSlot * 2 + (p.bSeen ? 1 : 0);
    }
  }
  return -1;
}

// ---------------------------------------------------------------------------
// Drawing, in the UI kit's list.

UiColor Alpha(UiColor c, float a) {
  c.a *= a;
  return c;
}

void Box(const HudStatsAt &at, double x, double y, double w, double h, UiColor c, double r = 0) {
  if (w <= 0 || h <= 0)
    return;
  UiFill(static_cast<float>(at.x + x * at.k), static_cast<float>(at.y + y * at.k), static_cast<float>(w * at.k),
         static_cast<float>(h * at.k), Alpha(c, at.fAlpha), static_cast<float>(r * at.k));
}

// Text in the box's units: its left at x (centred with nAlign 1, right with 2), its middle on y; with
// bShadow the page's text-shadow under it, which keeps it read over the map with no backing.
double Text(const HudStatsAt &at, UiFace face, double size, double spacing, UiColor c, const std::string &s, double x,
            double y, int nAlign, bool bShadow = false) {
  if (s.empty() || size <= 0)
    return 0;
  const float fSize = static_cast<float>(size * at.k), fSpacing = static_cast<float>(spacing * at.k);
  const float fW = UiTextWidth(face, fSize, fSpacing, s.c_str());
  float fAscent = 0, fDescent = 0, fGap = 0;
  UiFontMetrics(face, fSize, &fAscent, &fDescent, &fGap);
  const float px = static_cast<float>(at.x + x * at.k), py = static_cast<float>(at.y + y * at.k);
  const float fX = nAlign == 1 ? px - fW / 2 : nAlign == 2 ? px - fW : px;
  const float fBase = py + (fAscent + fDescent) / 2;
  if (bShadow) {
    const float d = static_cast<float>(at.k);
    UiText(fX, fBase + d, face, fSize, fSpacing, UiColor{0, 0, 0, 0.8f * c.a * at.fAlpha}, s.c_str());
    UiText(fX + d * 0.6f, fBase + d * 0.6f, face, fSize, fSpacing, UiColor{0, 0, 0, 0.5f * c.a * at.fAlpha}, s.c_str());
  }
  UiText(fX, fBase, face, fSize, fSpacing, Alpha(c, at.fAlpha), s.c_str());
  return fW / at.k;
}

// Text cut to a width, with an ellipsis.
std::string Elide(UiFace face, double size, double spacing, const std::string &s, double most) {
  if (Width(face, size, spacing, s) <= most)
    return s;
  std::string t = s;
  while (!t.empty()) {
    t.pop_back();
    while (!t.empty() && (static_cast<unsigned char>(t.back()) & 0xC0) == 0x80)
      t.pop_back();
    if (Width(face, size, spacing, t + "\xE2\x80\xA6") <= most)
      return t + "\xE2\x80\xA6";
  }
  return std::string();
}

UiColor Rgb(unsigned int n, float a = 1.0f) { return UiRgb(n, a); }

// the race's one letter beside a name
const char *const kRace[3] = {PTC_("race letter", "Z"), PTC_("race letter", "T"), PTC_("race letter", "P")};

// The command icons, 1.17's cmdicons.grp, each frame a unit type's, through the console's colours
// (ticon.pcx's first row, as statcmd's buttons draw them), on a sheet of 36-pixel cells in the
// picture atlas's slot 40 (port_ui.h); a type with no icon shows its initials.
const int kIconSlot = 40, kIconCell = 36, kIconsAcross = UI_PICTURE_SIDE / kIconCell;
struct Icons {
  bool bTried = false;
  void *pGrp = NULL;
  DWORD cb = 0;
  std::vector<unsigned char> sheet;
  int anCell[228];
  int anW[228], anH[228];
  int nUsed = 0;
  bool bDirty = false;
};
Icons g_icons;

int IconOf(int t) {
  Icons &ic = g_icons;
  if (!ic.bTried) {
    ic.bTried = true;
    for (int i = 0; i < 228; ++i)
      ic.anCell[i] = -1;
    if (!SFileLoadFile("unit\\cmdbtns\\cmdicons.grp", &ic.pGrp, &ic.cb, 0, NULL))
      ic.pGrp = NULL;
    ic.sheet.assign(static_cast<size_t>(UI_PICTURE_SIDE) * UI_PICTURE_SIDE * 4, 0);
  }
  if (t < 0 || t >= 228 || ic.pGrp == NULL)
    return -1;
  if (ic.anCell[t] != -1)
    return ic.anCell[t];
  ic.anCell[t] = -2;
  if (ic.nUsed >= kIconsAcross * kIconsAcross)
    return -2;
  const BYTE *p = static_cast<const BYTE *>(ic.pGrp);
  const int nFrames = ic.cb >= 6 ? p[0] | p[1] << 8 : 0;
  const size_t at = 6 + static_cast<size_t>(t) * 8;
  if (t >= nFrames || at + 8 > ic.cb)
    return -2;
  DWORD dwOff;
  memcpy(&dwOff, p + at + 4, sizeof dwOff);
  dwOff &= 0x7FFFFFFFu;
  FRAME f;
  memset(&f, 0, sizeof f);
  f.bWidth = p[at + 2];
  f.bHeight = p[at + 3];
  f.pData = const_cast<BYTE *>(p + dwOff);
  if (f.bWidth == 0 || f.bHeight == 0 || f.bWidth > kIconCell || f.bHeight > kIconCell || dwOff + 2u * f.bHeight > ic.cb)
    return -2;
  std::vector<int> px;
  PortGrpFrameDecode(f, &px);
  if (px.size() < static_cast<size_t>(f.bWidth) * f.bHeight)
    return -2;
  unsigned char rgb[768];
  PortPresentPaletteRGB(rgb);
  const int n = ic.nUsed++;
  const int x0 = (n % kIconsAcross) * kIconCell, y0 = (n / kIconsAcross) * kIconCell;
  for (int y = 0; y < f.bHeight; ++y)
    for (int x = 0; x < f.bWidth; ++x) {
      int v = px[static_cast<size_t>(y) * f.bWidth + x];
      if (v < 0)
        continue;
      if (v < 16)
        v = sgBmpTIcon_g[v];  // the console's colours for a button that can be pressed
      unsigned char *q = &ic.sheet[(static_cast<size_t>(y0 + y) * UI_PICTURE_SIDE + x0 + x) * 4];
      memcpy(q, &rgb[v * 3], 3);
      q[3] = 255;
    }
  ic.anW[t] = f.bWidth;
  ic.anH[t] = f.bHeight;
  ic.anCell[t] = n;
  ic.bDirty = true;
  return n;
}

// A unit type's initials, as the page shows them where the art is missing.
std::string Initials(int t) {
  const char *psz = t >= 0 && t < 228 ? get_unit_name_from_type(static_cast<WORD>(t)) : NULL;
  // the name without its race ("Terran Marine")
  for (const char *pszRace : {"Terran ", "Zerg ", "Protoss "})
    if (psz != NULL && strncmp(psz, pszRace, strlen(pszRace)) == 0)
      psz += strlen(pszRace);
  std::string s;
  for (bool bStart = true; psz != NULL && *psz != '\0' && s.size() < 2; ++psz) {
    if (bStart && *psz != ' ')
      s += *psz;
    bStart = *psz == ' ';
  }
  return s;
}

// The eye's almond, its four curves (the path M2 12s3.6-6 10-6 10 6 10 6-3.6 6-10 6S2 12 2 12z on a
// 24-unit square) as lines.
void Almond(float x, float y, float u, UiColor c, float fWidth) {
  static const float kCurves[4][8] = {{2, 12, 2, 12, 5.6f, 6, 12, 6},
                                      {12, 6, 18.4f, 6, 22, 12, 22, 12},
                                      {22, 12, 22, 12, 18.4f, 18, 12, 18},
                                      {12, 18, 5.6f, 18, 2, 12, 2, 12}};
  for (const float *b : kCurves) {
    float px = x + b[0] * u, py = y + b[1] * u;
    for (int i = 1; i <= 8; ++i) {
      const float t = i / 8.0f, s = 1 - t;
      const float k0 = s * s * s, k1 = 3 * s * s * t, k2 = 3 * s * t * t, k3 = t * t * t;
      const float qx = x + (k0 * b[0] + k1 * b[2] + k2 * b[4] + k3 * b[6]) * u;
      const float qy = y + (k0 * b[1] + k1 * b[3] + k2 * b[5] + k3 * b[7]) * u;
      UiLine(px, py, qx, qy, c, fWidth);
      px = qx, py = qy;
    }
  }
}

// The side view (hud.html's .pstats.side): a row a player on a shade of the game's background as
// solid as backing, the player's colour at its edge, the name and race, the figures and APM, and
// under it a rule as long as the army's value against the largest.
void DrawSide(const HudStatsAt &at, const HudStatsLook &look, double backing, const double rowH[2],
              const PortHudState &st) {
  const Side sd = SideOf(at.h, rowH, st);
  double top = 1;
  for (int i = 0; i < st.nPlayers; ++i)
    top = (std::max)(top, static_cast<double>(st.aPlayers[i].nArmyValue));
  const UiColor shade = Rgb(0x080b10, static_cast<float>(backing));
  const bool bShadow = true;
  const double w = at.w, rh = sd.rh;
  for (const SideRow &r : sd.rows) {
    if (backing > 0)
      Box(at, 0, r.y, w, rh, shade);
    if (r.nPlayer < 0) {
      // {0} how many players the view has no room for
      const std::string s = PortTextF("+{0} more \xC2\xB7 F1", {std::to_string(sd.nMore)});
      Text(at, UI_FACE_TEXT, 13, 13 * 0.08, look.lamp, s, w / 2, r.y + rh / 2, 1, bShadow);
      continue;
    }
    const PortHudPlayer &p = st.aPlayers[r.nPlayer];
    const UiColor pc = Rgb(p.nColour);
    Box(at, 0, r.y, 4, rh, pc);
    const double size = sd.bTwo ? 14 : 13, keySize = size * 0.7, sp = keySize * 0.08;
    // APM at the right: the figure, then its label
    const std::string apm = std::to_string(p.nApm);
    const double apmW = Width(UI_FACE_TEXT, size, 0, apm) + size * 0.3 + Width(UI_FACE_TEXT, keySize, sp, "APM");
    const double right = w - 9;
    const double y1 = sd.bTwo ? r.y + rh / 2 - 3 - size * 0.5 : r.y + rh / 2;  // the first line's middle
    Text(at, UI_FACE_TEXT, keySize, sp, look.label, "APM", right, y1, 2, bShadow);
    Text(at, UI_FACE_TEXT, size, 0, look.text, apm, right - Width(UI_FACE_TEXT, keySize, sp, "APM") - size * 0.3, y1, 2,
         bShadow);
    // the figures: a letter and its figure each, 9 apart
    const std::string vals[4] = {Fmt(p.nMinerals), Fmt(p.nGas),
                                 std::to_string(p.nSupplyUsed) + "/" + std::to_string(p.nSupplyMax),
                                 std::to_string(p.nWorkers)};
    // the one letter before each figure: minerals, gas, supply, workers
    static const char *const kKeys[4] = {PTC_("figure letter", "M"), PTC_("figure letter", "G"),
                                         PTC_("figure letter", "S"), PTC_("figure letter", "W")};
    const int nFirst = sd.bTwo ? 0 : 2;
    double widths[4] = {}, total = 0;
    for (int i = nFirst; i < 4; ++i)
      total += widths[i] = Width(UI_FACE_TEXT, keySize, sp, PortTextC("figure letter", kKeys[i])) + size * 0.3 +
                           Width(UI_FACE_TEXT, size, 0, vals[i]);
    double nameRight;
    if (sd.bTwo) {
      // the second line: the four spread over the row's width after the colour
      const double x0 = 13, span = right - x0, step = (std::max)(9.0, (span - total) / 3);
      const double y2 = r.y + rh / 2 + 3 + size * 0.5;
      double x = x0;
      for (int i = 0; i < 4; ++i) {
        const double wk =
            Text(at, UI_FACE_TEXT, keySize, sp, look.label, PortTextC("figure letter", kKeys[i]), x, y2, 0, bShadow);
        Text(at, UI_FACE_TEXT, size, 0, look.text, vals[i], x + wk + size * 0.3, y2, 0, bShadow);
        x += widths[i] + step;
      }
      nameRight = right - apmW - 9;
    } else {
      const double fgW = total + 9;
      double x = right - apmW - 9 - fgW;
      nameRight = x - 9;
      for (int i = nFirst; i < 4; ++i) {
        const double wk =
            Text(at, UI_FACE_TEXT, keySize, sp, look.label, PortTextC("figure letter", kKeys[i]), x, y1, 0, bShadow);
        Text(at, UI_FACE_TEXT, size, 0, look.text, vals[i], x + wk + size * 0.3, y1, 0, bShadow);
        x += widths[i] + 9;
      }
    }
    const std::string name = Elide(UI_FACE_BOLD, size, size * 0.04,
                                   std::string(p.szName) + " " + PortTextC("race letter", kRace[p.nRace]) +
                                       (p.szLevel[0] ? std::string(" \xC2\xB7 ") + p.szLevel : std::string()),
                                   nameRight - 13);
    Text(at, UI_FACE_BOLD, size, size * 0.04, look.figure, name, 13, y1, 0, bShadow);
    // the army's value, a rule under the row
    Box(at, 4, r.y + rh - 2, (w - 4) * p.nArmyValue / top, 2, Alpha(pc, 0.85f));
  }
}

// The graph: a line a player in their colour, the page's figure over the game, ending on the
// figures the rows show; the times at its ends and the figures at its lines.
double NiceMax(double v) {
  if (v <= 0)
    return 1;
  const double p = pow(10.0, floor(log10(v))), m = v / p;
  return (m <= 1 ? 1 : m <= 2 ? 2 : m <= 5 ? 5 : 10) * p;
}

std::string Clock(unsigned nFrames) {
  const unsigned s = static_cast<unsigned>(static_cast<unsigned long long>(nFrames) * 42 / 1000);
  char sz[32];
  if (s >= 3600)
    snprintf(sz, sizeof sz, "%u:%02u:%02u", s / 3600, s / 60 % 60, s % 60);
  else
    snprintf(sz, sizeof sz, "%u:%02u", s / 60, s % 60);
  return sz;
}

void DrawChart(const HudStatsAt &at, const HudStatsLook &look, const Popup &pp, const PortHudState &st, int nPage) {
  const double w = at.w;
  Box(at, 0, pp.chartY, w, 1, Alpha(look.label, 0.25f));
  Text(at, UI_FACE_TEXT, 11, 11 * 0.1, look.label, Upper(PortText(kChartTitles[nPage])), 14, pp.chartY + 10 + 5.5, 0);
  const double X = 14, Y = pp.chartY + 25, W = w - 28, H = pp.chartH - 31;
  if (W <= 0 || H <= 0)
    return;
  const int k = nPage + 1;  // the sample's figure
  // the samples, and now
  struct Pt {
    unsigned nFrame;
    const std::vector<std::array<int, 5>> *pV;
  };
  std::vector<std::array<int, 5>> nowV;
  for (int i = 0; i < st.nPlayers; ++i) {
    const PortHudPlayer &p = st.aPlayers[i];
    nowV.push_back({p.nSlot, p.nSupplyUsed, p.nIncomeMinerals + p.nIncomeGas, p.nArmyValue, p.nLostValue});
  }
  std::vector<Pt> hist;
  // at most about a point every three units across
  const size_t nMost = static_cast<size_t>((std::max)(2.0, W / 3));
  const size_t stride = g_samples.size() > nMost ? (g_samples.size() + nMost - 1) / nMost : 1;
  for (size_t i = 0; i < g_samples.size(); i += stride)
    hist.push_back({g_samples[i].nFrame, &g_samples[i].v});
  if (hist.empty() || hist.back().nFrame < st.nFrame)
    hist.push_back({st.nFrame, &nowV});
  if (hist.size() < 2) {
    Text(at, UI_FACE_TEXT, 13, 0, look.label, PortText("The graph fills in as the game plays."), X + W / 2,
         Y + H / 2, 1);
    return;
  }
  const double L = 52, R = 14, T = 8, B = 22;
  const unsigned t0 = hist.front().nFrame, t1 = hist.back().nFrame;
  double topV = 0;
  for (const Pt &h : hist)
    for (const std::array<int, 5> &v : *h.pV)
      topV = (std::max)(topV, static_cast<double>(v[k]));
  topV = NiceMax(topV);
  auto px = [&](unsigned t) { return X + L + (W - L - R) * (t - t0) / (std::max)(1.0, static_cast<double>(t1 - t0)); };
  auto py = [&](double v) { return Y + T + (H - T - B) * (1 - v / topV); };
  const long long nTop = static_cast<long long>(topV);
  const int steps = topV >= 4 && nTop % 4 == 0 ? 4 : nTop % 5 == 0 ? 5 : nTop == 2 ? 2 : 1;
  for (int g = 0; g <= steps; ++g) {
    const double gv = topV * g / steps, gy = py(gv);
    Box(at, X + L, gy - 0.5, W - L - R, 1, Alpha(look.label, 0.22f));
    char sz[32];
    if (gv != floor(gv))
      snprintf(sz, sizeof sz, "%.1f", gv);
    Text(at, UI_FACE_TEXT, 11, 0, look.label, gv != floor(gv) ? std::string(sz) : Fmt(gv), X + L - 8, gy, 2);
  }
  Text(at, UI_FACE_TEXT, 11, 0, look.label, Clock(t0 - g_nFrom), X + L, Y + H - 9, 0);
  Text(at, UI_FACE_TEXT, 11, 0, look.label, Clock(t1 - g_nFrom), X + W - R, Y + H - 9, 2);
  for (int i = 0; i < st.nPlayers; ++i) {
    const PortHudPlayer &p = st.aPlayers[i];
    const UiColor c = Alpha(Rgb(p.nColour), at.fAlpha);
    bool bHave = false;
    float lx = 0, ly = 0;
    for (const Pt &h : hist)
      for (const std::array<int, 5> &v : *h.pV) {
        if (v[0] != p.nSlot)
          continue;
        const float x = static_cast<float>(at.x + px(h.nFrame) * at.k), y = static_cast<float>(at.y + py(v[k]) * at.k);
        if (bHave)
          UiLine(lx, ly, x, y, c, static_cast<float>(2 * at.k));
        bHave = true;
        lx = x, ly = y;
      }
    if (bHave) {
      const float r = static_cast<float>(3 * at.k);
      UiFill(lx - r, ly - r, 2 * r, 2 * r, c, r);
    }
  }
}

// The popup (hud.html's .pstats.popup): the header with the pages, the column headings, a row a
// player under a heading for each team with its sums, and the graph where there is room.
void DrawPopup(const HudStatsAt &at, const HudStatsLook &look, const double rowH[2], const PortHudState &st) {
  const Popup pp = PopupOf(at.w, at.h, rowH, st, g_nPage);
  const double w = at.w, scroll = ScrollOf(pp);
  // the header
  Text(at, UI_FACE_BOLD, 17, 17 * 0.18, look.figure, PortText("PLAYERS"), 14, 20, 0);
  Text(at, UI_FACE_TEXT, 12, 12 * 0.08, look.label, SumText(st, pp), pp.sumX, 20, 0);
  for (int i = 0; i < HUD_STATS_PAGES; ++i) {
    const bool bOn = i == g_nPage;
    Text(at, UI_FACE_TEXT, 12, 12 * 0.14, bOn ? look.figure : look.label, Upper(PortText(kPageNames[i])),
         pp.tabX[i] + pp.tabW[i] / 2, 20, 1);
    if (bOn)
      Box(at, pp.tabX[i], 32, pp.tabW[i], 2, look.lamp);
  }
  if (pp.bKeys)
    Text(at, UI_FACE_TEXT, 11, 11 * 0.06, look.label, PortText(kKeysHint), pp.keysX, 20, 0);
  {
    const bool bHot = g_nOwnTip == 401;
    const float x0 = static_cast<float>(at.x + (pp.closeX + 8) * at.k), y0 = static_cast<float>(at.y + 12 * at.k);
    const float u = static_cast<float>(16 * at.k / 24);
    const UiColor c = Alpha(bHot ? look.figure : look.label, at.fAlpha);
    UiLine(x0 + 6 * u, y0 + 6 * u, x0 + 18 * u, y0 + 18 * u, c, 2 * u);
    UiLine(x0 + 18 * u, y0 + 6 * u, x0 + 6 * u, y0 + 18 * u, c, 2 * u);
  }
  Box(at, 0, kHeaderH, w, 1, Alpha(look.label, 0.3f));
  // the column headings
  const double headY = kHeaderH + 9 + 5.5;
  Text(at, UI_FACE_TEXT, 11, 11 * 0.1, look.label, PortText("PLAYER"), pp.plX, headY, 0);
  for (const Col &c : pp.cols) {
    const std::string head = Upper(PortText(c.def.pszHead));
    if (c.def.nHow == HOW_NUM)
      Text(at, UI_FACE_TEXT, 11, 11 * 0.1, look.label, Elide(UI_FACE_TEXT, 11, 1.1, head, c.w), c.x + c.w, headY, 2);
    else if (c.def.nHow == HOW_EYE)
      Text(at, UI_FACE_TEXT, 11, 11 * 0.1, look.label, head, c.x + c.w / 2, headY, 1);
    else
      Text(at, UI_FACE_TEXT, 11, 11 * 0.1, look.label, Elide(UI_FACE_TEXT, 11, 1.1, head, c.w), c.x, headY, 0);
  }
  // the bars' largest figures
  double tops[16] = {};
  for (size_t ci = 0; ci < pp.cols.size() && ci < 16; ++ci)
    for (int i = 0; i < st.nPlayers; ++i)
      tops[ci] = (std::max)(tops[ci], Value(st.aPlayers[i], pp.cols[ci].def.nKey));
  // the body, clipped to its box and scrolled
  const float cx0 = static_cast<float>(at.x), cy0 = static_cast<float>(at.y + pp.bodyY * at.k);
  UiPushClip(cx0, cy0, static_cast<float>(at.x + w * at.k), static_cast<float>(at.y + (pp.bodyY + pp.bodyH) * at.k));
  const double size = (std::max)(12.0, (std::min)(pp.rh, 32.0) * 0.5);
  int nChild = 0;
  for (const Line &ln : pp.lines) {
    ++nChild;
    const double y = pp.bodyY + ln.y - scroll, mid = y + ln.h / 2;
    if (y + ln.h < pp.bodyY || y > pp.bodyY + pp.bodyH)
      continue;
    if (ln.nPlayer < 0) {
      // a team's heading, with its sums
      const std::vector<int> &team = pp.teams[ln.nTeam];
      Text(at, UI_FACE_TEXT, 11, 1.1, look.label,
           PortTextF("TEAM {0}", {std::to_string(st.aPlayers[team[0]].nTeam)}), pp.plX, mid, 0);
      for (const Col &c : pp.cols) {
        if (c.def.nHow == HOW_UNITS || c.def.nHow == HOW_EYE || c.def.nKey == K_APM)
          continue;
        double sum = 0;
        int used = 0, most = 0;
        for (int i : team) {
          sum += Value(st.aPlayers[i], c.def.nKey);
          used += st.aPlayers[i].nSupplyUsed;
          most += st.aPlayers[i].nSupplyMax;
        }
        const std::string s = c.def.nKey == K_SUPPLY ? std::to_string(used) + "/" + std::to_string(most) : Fmt(sum);
        Text(at, UI_FACE_TEXT, 11, 1.1, look.label, s, c.x + c.w, mid, 2);
      }
      Box(at, 0, y + ln.h - 1, w, 1, Alpha(look.label, 0.25f));
      continue;
    }
    const PortHudPlayer &p = st.aPlayers[ln.nPlayer];
    const UiColor pc = Rgb(p.nColour);
    if (nChild % 2 == 0)
      Box(at, 0, y, w, ln.h, Alpha(look.text, 0.04f));
    // the player: the colour, the name, the race
    Box(at, pp.plX, y + 3, 4, ln.h - 6, pc);
    const std::string race =
        std::string(PortTextC("race letter", kRace[p.nRace])) + (p.szLevel[0] ? std::string(" ") + p.szLevel : std::string());
    const double raceW = Width(UI_FACE_TEXT, size * 0.75, size * 0.075, race);
    const std::string name = Elide(UI_FACE_BOLD, size, size * 0.04, p.szName, pp.plW - 13 - 9 - raceW);
    const double nameW = Text(at, UI_FACE_BOLD, size, size * 0.04, Alpha(look.figure, p.bSeen ? 1.0f : 0.6f), name,
                              pp.plX + 13, mid, 0);
    Text(at, UI_FACE_TEXT, size * 0.75, size * 0.075, look.label, race, pp.plX + 13 + nameW + 9, mid, 0);
    for (size_t ci = 0; ci < pp.cols.size(); ++ci) {
      const Col &c = pp.cols[ci];
      const double v = Value(p, c.def.nKey);
      const std::string txt =
          c.def.nKey == K_SUPPLY ? std::to_string(p.nSupplyUsed) + "/" + std::to_string(p.nSupplyMax) : Fmt(v);
      switch (c.def.nHow) {
        case HOW_NUM:
          Text(at, UI_FACE_TEXT, size, 0, look.figure, txt, c.x + c.w, mid, 2);
          break;
        case HOW_BAR: {
          const double numW = size * 4.6, barW = (std::max)(0.0, c.w - numW - 10);
          const double share = ci < 16 && tops[ci] > 0 ? v / tops[ci] : 0;
          Box(at, c.x, mid - 3, barW, 6, Alpha(look.label, 0.16f));
          Box(at, c.x, mid - 3, barW * share, 6, pc);
          Text(at, UI_FACE_TEXT, size, 0, look.figure, txt, c.x + c.w, mid, 2);
          break;
        }
        case HOW_UNITS: {
          if (p.nUnitTypes == 0) {
            Text(at, UI_FACE_TEXT, size, 0, look.label, PortText("none"), c.x, mid, 0);
            break;
          }
          UiPushClip(static_cast<float>(at.x + c.x * at.k), cy0, static_cast<float>(at.x + (c.x + c.w) * at.k),
                     static_cast<float>(at.y + (pp.bodyY + pp.bodyH) * at.k));
          const double icon = (std::min)(pp.rh, 32.0) * 0.8;
          double x = c.x;
          for (int u = 0; u < p.nUnitTypes && x < c.x + c.w; ++u) {
            const int t = p.awUnitType[u], nCell = IconOf(t);
            if (nCell >= 0) {
              // the icon whole in its square, a pixel of 1.17's a square of the screen's
              const double iw = g_icons.anW[t], ih = g_icons.anH[t], s = icon / (std::max)(iw, ih);
              const float u0 = static_cast<float>((nCell % kIconsAcross) * kIconCell);
              const float v0 = static_cast<float>((nCell / kIconsAcross) * kIconCell);
              UiPicturePart(static_cast<float>(at.x + (x + (icon - iw * s) / 2) * at.k),
                            static_cast<float>(at.y + (mid - ih * s / 2) * at.k), static_cast<float>(iw * s * at.k),
                            static_cast<float>(ih * s * at.k), kIconSlot, u0, v0, u0 + static_cast<float>(iw),
                            v0 + static_cast<float>(ih), UiColor{1, 1, 1, at.fAlpha}, true);
            } else {
              const float bx = static_cast<float>(at.x + x * at.k), by = static_cast<float>(at.y + (mid - icon / 2) * at.k);
              UiRing(bx, by, static_cast<float>(icon * at.k), static_cast<float>(icon * at.k),
                     Alpha(look.label, 0.4f * at.fAlpha), 0.0f, static_cast<float>(at.k));
              Text(at, UI_FACE_BOLD, size * 0.6, 0, look.label, Initials(t), x + icon / 2, mid, 1);
            }
            x += icon + 4;
            x += Text(at, UI_FACE_TEXT, size, 0, look.figure, std::to_string(p.awUnitCount[u]), x, mid, 0) + 12;
          }
          UiPopClip();
          break;
        }
        case HOW_EYE: {
          if (!p.bEye) {
            Text(at, UI_FACE_TEXT, size, 0, look.label, "\xE2\x80\x93", c.x + c.w / 2, mid, 1);
            break;
          }
          const double e = (std::min)(pp.rh, 30.0) * 0.85;
          const UiColor ec = p.bSeen ? look.lamp : Alpha(look.label, 0.55f);
          HudStatsEye(static_cast<float>(at.x + (c.x + (c.w - e) / 2 + 2) * at.k),
                      static_cast<float>(at.y + (mid - e / 2 + 2) * at.k), static_cast<float>((e - 4) * at.k),
                      Alpha(ec, at.fAlpha));
          break;
        }
      }
    }
  }
  UiPopClip();
  // the scroll's place, when the rows run past the body
  if (g_fScrollMost > 0) {
    const double barH = pp.bodyH * pp.bodyH / pp.contentH, barY = pp.bodyY + (pp.bodyH - barH) * scroll / g_fScrollMost;
    Box(at, w - 5, barY, 3, barH, Alpha(look.label, 0.45f), 1.5);
  }
  if (pp.chartH > 0)
    DrawChart(at, look, pp, st, g_nPage);
}

// A view in a place of its own (port_hudstats.h): the side view at the right edge from 112 units
// down on its own backing, the popup in the middle on a plate in the interface's colours.
void OwnPlace(int nView, float fX, float fY, float fW, float fH, HudStatsAt *pAt) {
  const double k = fH / 1080.0, across = fW / k;
  if (nView == HUD_STATS_SIDE) {
    pAt->w = 300, pAt->h = 540;
    pAt->x = fX + (across - 16 - pAt->w) * k;
  } else {
    pAt->w = (std::min)(1000.0, across - 32), pAt->h = 640;
    pAt->x = fX + (across - pAt->w) / 2 * k;
  }
  pAt->y = fY + 112 * k;
  pAt->k = k;
  pAt->fAlpha = 1.0f;
}

HudStatsLook OwnLook(void) { return HudStatsLook{FrontKit::kInk, FrontKit::kInk, FrontKit::kInkDim, FrontKit::kGold}; }

bool OwnShown(int nView) { return PortHudStatsOn(nView) && !PortHudStatsPlaced(nView); }

// The point x, y of the window in a view drawn last, in its units; false when it was not drawn of late.
bool InDrawn(int nView, float x, float y, double *plx, double *ply) {
  const Drawn &d = g_drawn[nView];
  if (!PortHudStatsOn(nView) || d.nAt == 0 || SDL_GetTicks() - d.nAt > 500 || d.at.k <= 0)
    return false;
  *plx = (x - d.at.x) / d.at.k;
  *ply = (y - d.at.y) / d.at.k;
  return *plx >= 0 && *ply >= 0 && *plx < d.at.w && *ply < d.at.h;
}

// ---------------------------------------------------------------------------
// The layer: the views in places of their own, and the keys, the wheel and F1's hold for both.

bool LayerShown(void) {
  // F1 held past its time keeps the popup only while it is down
  if (g_bHoldWatch) {
    const bool bDown = WinMsgKeyState(VK_F1) < 0;
    if (bDown && !g_bHeld && SDL_GetTicks() - g_nHoldFrom > HOLD_MS)
      g_bHeld = true;
    if (!bDown) {
      const bool bClose = g_bHeld;
      g_bHoldWatch = g_bHeld = false;
      if (bClose)
        PortHudStatsSet(HUD_STATS_POPUP, false);
    }
  }
  return PortHudStatsOn(HUD_STATS_SIDE) || PortHudStatsOn(HUD_STATS_POPUP);
}

bool LayerDraw(int nWidth, int nHeight, float fX, float fY, float fW, float fH) {
  (void)nWidth, (void)nHeight;
  bool bDrew = false;
  PortHudState st;
  for (int v = 0; v < HUD_STATS_VIEWS; ++v) {
    if (!OwnShown(v))
      continue;
    if (!bDrew)
      PortHudGet(&st);
    bDrew = true;
    HudStatsAt at;
    OwnPlace(v, fX, fY, fW, fH, &at);
    const HudStatsLook look = OwnLook();
    if (v == HUD_STATS_POPUP) {
      // the plate: the interface's panel, its rule round it, a shadow under it
      const float x = static_cast<float>(at.x), y = static_cast<float>(at.y);
      const float w = static_cast<float>(at.w * at.k), h = static_cast<float>(at.h * at.k), k = static_cast<float>(at.k);
      UiFill(x - 6 * k, y + 6 * k, w + 12 * k, h + 12 * k, UiColor{0, 0, 0, 0.35f}, 12 * k);
      UiFill(x, y, w, h, Alpha(FrontKit::kPanelSolid, 0.95f), 4 * k);
      UiRing(x, y, w, h, FrontKit::kRule, 4 * k, k);
    }
    HudStatsDraw(v, at, look, 0.38, v == HUD_STATS_POPUP ? kPopupRowH : kSideRowH, st);
    g_drawn[v].bOwn = true;
  }
  // a view of its own's tooltip, under the pointer
  if (bDrew && g_nOwnTip >= 0 && !g_ownTip.empty()) {
    const float k = fH / 1080.0f, size = 12 * k;
    const float tw = UiTextWidth(UI_FACE_TEXT, size, 0, g_ownTip.c_str());
    float ascent, descent, gap;
    UiFontMetrics(UI_FACE_TEXT, size, &ascent, &descent, &gap);
    float x = g_mx + 14 * k, y = g_my + 20 * k;
    x = (std::min)(x, fX + fW - tw - 16 * k);
    UiFill(x, y, tw + 16 * k, 26 * k, Alpha(FrontKit::kPanelSolid, 0.97f), 3 * k);
    UiRing(x, y, tw + 16 * k, 26 * k, FrontKit::kRule, 3 * k, k);
    UiText(x + 8 * k, y + 13 * k + (ascent + descent) / 2, UI_FACE_TEXT, size, 0, FrontKit::kInk, g_ownTip.c_str());
  }
  return bDrew;
}

bool LayerMouse(unsigned uMsg, unsigned long long wParam, long long lParam) {
  (void)wParam;
  const float x = static_cast<float>(static_cast<short>(LOWORD(lParam)));
  const float y = static_cast<float>(static_cast<short>(HIWORD(lParam)));
  g_mx = x, g_my = y;
  if (!PortHudStatsOn(HUD_STATS_SIDE) && !PortHudStatsOn(HUD_STATS_POPUP)) {
    g_nOwnTip = -1;
    return false;
  }
  // the views drawn in places of their own; a design's slots take the mouse through the HUD
  PortHudState st;
  On on;
  for (int v = HUD_STATS_VIEWS - 1; v >= 0 && on.nWhat == ON_NONE; --v) {
    double lx, ly;
    if (!g_drawn[v].bOwn || !OwnShown(v) || !InDrawn(v, x, y, &lx, &ly))
      continue;
    PortHudGet(&st);
    on = v == HUD_STATS_POPUP ? OnPopup(g_drawn[v].at.w, g_drawn[v].at.h, kPopupRowH, lx, ly, st)
                              : OnSide(g_drawn[v].at.h, kSideRowH, lx, ly, st, g_drawn[v].at.w);
  }
  std::string key;
  const int nTip = on.nWhat == ON_NONE ? -1 : TipOf(on, st, &g_ownTip, &key);
  if (!key.empty())
    g_ownTip += " (" + key + ")";
  if (nTip != g_nOwnTip) {
    g_nOwnTip = nTip;
    PortPresentRedraw();
  }
  if (on.nWhat == ON_NONE)
    return false;
  if (uMsg == WM_LBUTTONDOWN || uMsg == WM_LBUTTONDBLCLK)
    Act(on, st);
  // the popup's presses are its own; the moves go on to the game
  return uMsg != WM_MOUSEMOVE;
}

// The keys come before 1.17's accelerators (PORT_OVERLAY_BEFORE_ACCEL), which make F1 Help, and so
// make no characters; a menu over the game (the game menu) keeps its own.
bool LayerKey(unsigned vk, bool bDown, bool bRepeat) {
  if (!bDown || !PortHudStatsAllowed() || gbInMsgMode || PortOverlayHas(PORT_OVERLAY_HIDES_CURSOR))
    return false;
  const bool bShift = WinMsgKeyState(VK_SHIFT) < 0;
  if (vk == VK_F1 && WinMsgKeyState(VK_CONTROL) >= 0) {
    if (bRepeat)
      return true;
    if (bShift) {
      PortHudStatsSet(HUD_STATS_SIDE, !g_abOn[HUD_STATS_SIDE]);
    } else if (g_abOn[HUD_STATS_POPUP]) {
      PortHudStatsSet(HUD_STATS_POPUP, false);
    } else {
      PortHudStatsSet(HUD_STATS_POPUP, true);
      g_bHoldWatch = true;
      g_bHeld = false;
      g_nHoldFrom = SDL_GetTicks();
    }
    return true;
  }
  if (!g_abOn[HUD_STATS_POPUP])
    return false;
  if (vk == VK_TAB) {
    if (!bRepeat)
      PortHudStatsTurn(g_nPage + (bShift ? -1 : 1));
    return true;
  }
  if (vk == VK_ESCAPE) {
    PortHudStatsSet(HUD_STATS_POPUP, false);
    return true;
  }
  return false;
}

bool LayerOutside(float x, float y) {
  double lx, ly;
  for (int v = 0; v < HUD_STATS_VIEWS; ++v) {
    if (!g_drawn[v].bOwn || !OwnShown(v) || !InDrawn(v, x, y, &lx, &ly))
      continue;
    if (v == HUD_STATS_POPUP)
      return true;
    PortHudState st;
    PortHudGet(&st);
    if (OnSide(g_drawn[v].at.h, kSideRowH, lx, ly, st, g_drawn[v].at.w).nWhat == ON_MORE)
      return true;
  }
  return false;
}

bool LayerWheel(int nDelta) {
  double lx, ly;
  if (!InDrawn(HUD_STATS_POPUP, g_mx, g_my, &lx, &ly))
    return false;
  g_fScroll = SDL_clamp(g_fScroll - nDelta / 120.0 * 60.0, 0.0, g_fScrollMost);
  PortPresentRedraw();
  return true;
}

const PortOverlayLayer kLayer = {"stats",    PORT_OVERLAY_STATS, PORT_OVERLAY_BEFORE_ACCEL,         LayerShown, NULL, LayerDraw, NULL,
                                 LayerMouse, LayerKey,           LayerOutside, LayerWheel};
PORT_OVERLAY_LAYER(kLayer);

}  // namespace

void HudStatsDraw(int nView, const HudStatsAt &at, const HudStatsLook &look, double backing, const double rowH[2],
                  const PortHudState &st) {
  if (nView < 0 || nView >= HUD_STATS_VIEWS || !(nView == HUD_STATS_POPUP ? st.bStatsPopup : st.bStatsSide) ||
      st.nPlayers <= 0 || at.w <= 0 || at.h <= 0)
    return;
  Drawn &d = g_drawn[nView];
  d.at = at;
  d.rowH[0] = rowH[0], d.rowH[1] = rowH[1];
  d.nAt = SDL_GetTicks();
  d.bOwn = false;
  if (nView == HUD_STATS_SIDE)
    DrawSide(at, look, backing, rowH, st);
  else
    DrawPopup(at, look, rowH, st);
  if (g_icons.bDirty) {
    g_icons.bDirty = false;
    UiPictureSet(kIconSlot, g_icons.sheet.data(), UI_PICTURE_SIDE, UI_PICTURE_SIDE);
  }
}

bool HudStatsTakes(int nView, double w, double h, const double rowH[2], double lx, double ly, const PortHudState &st) {
  if (nView == HUD_STATS_POPUP)
    return st.bStatsPopup;
  return st.bStatsSide && OnSide(h, rowH, lx, ly, st, w).nWhat == ON_MORE;
}

void HudStatsPress(int nView, double w, double h, const double rowH[2], double lx, double ly, const PortHudState &st) {
  if (nView == HUD_STATS_POPUP)
    Act(OnPopup(w, h, rowH, lx, ly, st), st);
  else
    Act(OnSide(h, rowH, lx, ly, st, w), st);
}

int HudStatsTip(int nView, double w, double h, const double rowH[2], double lx, double ly, const PortHudState &st,
                std::string *pTitle, std::string *pKey, double q[4]) {
  const On on = nView == HUD_STATS_POPUP ? OnPopup(w, h, rowH, lx, ly, st) : OnSide(h, rowH, lx, ly, st, w);
  const int nId = TipOf(on, st, pTitle, pKey);
  if (nId >= 0)
    memcpy(q, on.q, sizeof on.q);
  return nId;
}

void HudStatsEye(float x, float y, float side, UiColor c) {
  const float u = side / 24.0f;
  Almond(x, y, u, c, 1.8f * u);
  UiFill(x + 9 * u, y + 9 * u, 6 * u, 6 * u, c, 3 * u);
}
