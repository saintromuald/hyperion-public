// port_teams.cpp -- teams a person chooses for a single-player game (port_teams.h).
#include <windows.h>
#include <math.h>
#include <stdlib.h>
#include <algorithm>
#include <map>

#include "port_teams.h"
#include "CUnit.h"
#include "port_session.h"  // Session
#include "../platform/port_util.h"  // PortLower
#include "../../h/hxrules.h"       // HX_OPT_TEAMS, g_hxRuleOptions
#include "../../h/playerlimits.h"  // PlayerSlots, PLAYER_SLOTS_MAX, PLAYER_NEUTRAL
#include "../../h/playertables.h"  // sgbAlliances_g, PlayersSeatSlot
#include "../../h/playervision.h"  // PlayerVisionAdd
#include "../../h/gametype.h"      // GameHeader
#include "../../h/net_cmds.h"      // PlayerData
#include "../render/port_teamcolor.h"  // PortRgbToLab, PortLabToRgb
#include "../front/port_frontscreens.h"  // FrontLog
#include "../front/port_text.h"
#include "../../gen/globals117.h"  // gPlayerData

namespace {

bool g_bThisGame;        // the game starting now had its teams chosen (port_session.h)
bool g_bStartsThisGame;  // and its starts

// The row a record came from (PlayersChosenColoursApply's mapping): the person row 0, the
// k-th computer seated 1 + k; -1 for neither.
int RowOf(int nFrom, int nType) {
  if (nType == 2)
    return 0;
  for (int k = 0; k < PLAYER_SLOTS_MAX; k++) {
    const int s = PlayersSeatSlot(k);
    if (s < 0)
      break;
    if (s == nFrom)
      return 1 + k;
  }
  return -1;
}

// 1 the computer, 2 a person: a slot that plays. 3 and 7, rescuable and neutral, stay
// allied with everyone, as 1.17 makes them.
bool Plays(int s) {
  const BYTE b = gPlayerData[s].nType;
  return b != 0 && b != 3 && b != 7 && s != PLAYER_NEUTRAL;
}

}  // namespace

void TeamsChoose(int nRow, int nTeam) {
  if (nRow < 0 || nRow > PLAYER_SLOTS_MAX)
    return;
  Session().bTeams = true;
  Session().aRow[nRow].nTeam = nTeam >= 0 && nTeam <= PLAYER_SLOTS_MAX ? nTeam : 0;
}

void TeamsChooseList(const char *pszList) {
  int nRow = 0;
  for (const char *p = pszList; p != NULL && *p != '\0' && nRow <= PLAYER_SLOTS_MAX;) {
    char *pEnd;
    const long n = strtol(p, &pEnd, 10);
    if (pEnd == p)
      break;
    TeamsChoose(nRow++, static_cast<int>(n));
    p = *pEnd == ',' ? pEnd + 1 : pEnd;
  }
}

void TeamsChooseStart(int nRow, int nStart) {
  if (nRow < 0 || nRow > PLAYER_SLOTS_MAX)
    return;
  Session().bStarts = true;
  Session().aRow[nRow].nStart = nStart >= 0 && nStart < PLAYER_SLOTS_MAX && nStart != PLAYER_NEUTRAL ? nStart : -1;
}

void TeamsChooseStartList(const char *pszList) {
  int nRow = 0;
  for (const char *p = pszList; p != NULL && *p != '\0' && nRow <= PLAYER_SLOTS_MAX;) {
    char *pEnd;
    const long n = strtol(p, &pEnd, 10);
    if (pEnd == p)
      break;
    TeamsChooseStart(nRow++, static_cast<int>(n));
    p = *pEnd == ',' ? pEnd + 1 : pEnd;
  }
}

int TeamsStartFor(int nFrom, int nType) {
  if (!g_bStartsThisGame)
    return -1;
  const int nRow = RowOf(nFrom, nType);
  if (nRow < 0)
    return -1;
  const int s = Session().aRow[nRow].nStart;
  return nType == 2 && s >= PLAYER_LOBBY_117 ? -1 : s;
}

bool TeamsStartTaken(int nSlot) {
  if (!g_bStartsThisGame)
    return false;
  for (int r = 0; r <= PLAYER_SLOTS_MAX; r++)
    if (Session().aRow[r].nStart == nSlot)
      return true;
  return false;
}

void TeamsSeat() {
  const PortSession &session = Session();
  g_bStartsThisGame = session.bStarts;
  g_bThisGame = session.bTeams;
  if (!g_bThisGame)
    return;
  const int n = PlayerSlots();
  for (int s = 0; s < n; s++)
    gPlayerData[s].nTeam = 0;
  // the local player, the one person in a single-player game, then the computers in the
  // order they were seated (PlayersSeatNote)
  for (int s = 0; s < n; s++)
    if (gPlayerData[s].nType == 2)
      gPlayerData[s].nTeam = static_cast<BYTE>(session.aRow[0].nTeam);
  for (int k = 0; k < PLAYER_SLOTS_MAX; k++) {
    const int s = PlayersSeatSlot(k);
    if (s < 0)
      break;
    gPlayerData[s].nTeam = static_cast<BYTE>(session.aRow[1 + k].nTeam);
  }
  for (int s = 0; s < n; s++)
    if (gPlayerData[s].nType != 0)
      FrontKit::FrontLog("teams: slot %d, type %d, team %d\n", s, gPlayerData[s].nType, gPlayerData[s].nTeam);
}

bool TeamsThisGame() {
  return g_bThisGame;
}

extern GameHeader gGameHeader;  // 0x6728D0 (gametype.cpp): a team game's teams

int TeamsSidesLeft(int *pnLast) {
  bool abHas[256] = {};
  for (CUnit *pUnit = CUnit::GetUsedList(); pUnit != NULL; pUnit = pUnit->pNext)
    abHas[pUnit->bPlayer] = true;
  const bool bTeams = (g_hxRuleOptions & HX_OPT_TEAMS) != 0 || gGameHeader.bNumTeams != 0;
  bool abTeam[256] = {};
  int n = 0, nLast = -1;
  for (int i = 0; i < 256; ++i) {
    if (!abHas[i] || i == PLAYER_NEUTRAL)
      continue;
    const int nTeam = bTeams && i < PlayerSlots() ? gPlayerData[i].nTeam : 0;
    if (nTeam != 0 && abTeam[nTeam])
      continue;
    abTeam[nTeam] = nTeam != 0;
    ++n;
    nLast = i;
  }
  if (pnLast != nullptr)
    *pnLast = n == 1 ? nLast : -1;
  return n;
}

void TeamsAlliances() {
  g_bThisGame = false;
  g_bStartsThisGame = false;
  if (gpLoadGameFile != NULL || (g_hxRuleOptions & HX_OPT_TEAMS) == 0)
    return;
  const int n = PlayerSlots();
  for (int i = 0; i < n; i++) {
    if (!Plays(i))
      continue;
    for (int j = 0; j < n; j++) {
      if (j == i || !Plays(j))
        continue;
      const bool bSame = gPlayerData[i].nTeam != 0 && gPlayerData[i].nTeam == gPlayerData[j].nTeam;
      sgbAlliances_g[i][j] = bSame ? 2 : 0;  // 2: allied, with allied victory
      if (bSame)
        PlayerVisionAdd(i, j);
    }
  }
}

// ---------------------------------------------------------------------------
// The layouts (port_teams.h), as the design center's teams.js works them out.

namespace {

double Dist(const TeamPoint &p, const TeamPoint &q) {
  return hypot(p.x - q.x, p.y - q.y);
}

int CountOf(const std::vector<int> &t) {
  int m = -1;
  for (int x : t)
    m = (std::max)(m, x);
  return m + 1;
}

// a split's key, its teams numbered in the order they first appear, so two that split alike match
std::vector<int> KeyOf(const std::vector<int> &t) {
  std::map<int, int> map;
  std::vector<int> key;
  for (int x : t) {
    if (map.find(x) == map.end()) {
      const int n = static_cast<int>(map.size());
      map[x] = n;
    }
    key.push_back(map[x]);
  }
  return key;
}

// each start's mean distance to its own team against the nearest other team's, averaged
double Score(const std::vector<TeamPoint> &P, const std::vector<int> &t) {
  const int n = static_cast<int>(P.size()), k = CountOf(t);
  if (k < 2)
    return 0.0;
  double tot = 0.0;
  for (int i = 0; i < n; ++i) {
    double own = 0.0;
    int on = 0;
    std::vector<double> other(k, 0.0);
    std::vector<int> oc(k, 0);
    for (int j = 0; j < n; ++j) {
      if (i == j)
        continue;
      const double d = Dist(P[i], P[j]);
      if (t[j] == t[i]) {
        own += d;
        ++on;
      } else {
        other[t[j]] += d;
        ++oc[t[j]];
      }
    }
    const double a = on ? own / on : 0.0;
    double b = INFINITY;
    for (int q = 0; q < k; ++q)
      if (q != t[i] && oc[q])
        b = (std::min)(b, other[q] / oc[q]);
    tot += b == INFINITY ? 0.0 : (b - a) / (std::max)((std::max)(a, b), 1e-6);
  }
  return tot / n;
}

// k centres, every team the same size, assigned nearest first: the first centre the start
// nearest the top left, each next the start farthest from those
std::vector<int> Clusters(const std::vector<TeamPoint> &P, int k) {
  const int n = static_cast<int>(P.size()), cap = (n + k - 1) / k;
  std::vector<TeamPoint> C;
  int first = 0;
  for (int i = 1; i < n; ++i)
    if (P[i].x + P[i].y < P[first].x + P[first].y)
      first = i;
  C.push_back(P[first]);
  while (static_cast<int>(C.size()) < k) {
    int best = 0;
    double bd = -1.0;
    for (int i = 0; i < n; ++i) {
      double md = INFINITY;
      for (const TeamPoint &c : C)
        md = (std::min)(md, Dist(P[i], c));
      if (md > bd) {
        bd = md;
        best = i;
      }
    }
    C.push_back(P[best]);
  }
  struct Pair {
    double d;
    int i, j;
  };
  std::vector<int> t;
  for (int it = 0; it < 24; ++it) {
    std::vector<Pair> pairs;
    for (int i = 0; i < n; ++i)
      for (int j = 0; j < k; ++j)
        pairs.push_back({Dist(P[i], C[j]), i, j});
    std::sort(pairs.begin(), pairs.end(), [](const Pair &a, const Pair &b) {
      return a.d != b.d ? a.d < b.d : a.i != b.i ? a.i < b.i : a.j < b.j;
    });
    std::vector<int> next(n, -1), size(k, 0);
    for (const Pair &x : pairs)
      if (next[x.i] < 0 && size[x.j] < cap) {
        next[x.i] = x.j;
        ++size[x.j];
      }
    const bool bMoved = KeyOf(next) != KeyOf(t.empty() ? std::vector<int>(n, -1) : t);
    t = next;
    for (int j = 0; j < k; ++j) {
      double sx = 0.0, sy = 0.0;
      int c = 0;
      for (int i = 0; i < n; ++i)
        if (t[i] == j) {
          sx += P[i].x;
          sy += P[i].y;
          ++c;
        }
      if (c)
        C[j] = {sx / c, sy / c};
    }
    if (!bMoved && it)
      break;
  }
  return t;
}

// a team's name: its centre's compass point from the map's middle
std::string PlaceName(const std::vector<TeamPoint> &P, const std::vector<int> &t, int q) {
  static const char *const kPoints[] = {PT_("East"),  PT_("South-east"), PT_("South"), PT_("South-west"),
                                        PT_("West"),  PT_("North-west"), PT_("North"), PT_("North-east")};
  double sx = 0.0, sy = 0.0;
  int c = 0;
  for (size_t i = 0; i < P.size(); ++i)
    if (t[i] == q) {
      sx += P[i].x;
      sy += P[i].y;
      ++c;
    }
  const double x = sx / c - 0.5, y = sy / c - 0.5;
  if (hypot(x, y) < 0.12)
    return PT_("Centre");
  const int n = static_cast<int>(floor(atan2(y, x) / (3.14159265358979 / 4) + 0.5));
  return kPoints[((n % 8) + 8) % 8];
}

std::string Word(int k) {
  static const char *const kWords[] = {"", "One", "Two", "Three", "Four", "Five", "Six", "Seven", "Eight",
                                       "Nine", "Ten", "Eleven", "Twelve", "Thirteen", "Fourteen", "Fifteen", "Sixteen"};
  return k >= 0 && k <= 16 ? kWords[k] : std::to_string(k);
}

}  // namespace

std::string TeamsVs(const std::vector<int> &teams) {
  std::vector<int> sizes(CountOf(teams), 0);
  for (int x : teams)
    if (x >= 0)
      ++sizes[x];
  bool bEven = sizes.size() > 2;
  for (int s : sizes)
    bEven = bEven && s == sizes[0];
  if (bEven)
    return std::to_string(sizes.size()) + " \xc3\x97 " + std::to_string(sizes[0]);
  std::string s;
  for (size_t i = 0; i < sizes.size(); ++i)
    s += (i ? " v " : "") + std::to_string(sizes[i]);
  return s;
}

const char *TeamsFit(double fScore) {
  return fScore >= 0.45f   ? PortText("Clean split")
         : fScore >= 0.25f ? PortText("Fair split")
                           : PortText("Loose split");
}

std::vector<TeamLayout> TeamsLayouts(const std::vector<TeamPoint> &P) {
  const int n = static_cast<int>(P.size());
  std::vector<TeamLayout> out;
  std::vector<std::vector<int>> seen;
  auto add = [&](const std::vector<int> &t, const char *pszName, bool bFfa, const std::string &line,
                 std::vector<std::string> names) {
    const std::vector<int> key = KeyOf(t);
    if (std::find(seen.begin(), seen.end(), key) != seen.end())
      return;
    seen.push_back(key);
    const int k = CountOf(t);
    if (names.empty())
      for (int q = 0; q < k; ++q)
        names.push_back(PlaceName(P, t, q));
    // two teams in the same place are told apart by number
    std::vector<std::string> named = names;
    for (int q = 0; q < k; ++q) {
      const int nSame = static_cast<int>(std::count(names.begin(), names.end(), names[q]));
      if (nSame > 1)
        named[q] = names[q] + " " + std::to_string(std::count(names.begin(), names.begin() + q + 1, names[q]));
    }
    TeamLayout l;
    l.name = pszName;
    l.vs = TeamsVs(t);
    l.line = line;
    l.teams = t;
    l.names = named;
    l.fScore = bFfa ? 0.0 : Score(P, t);
    l.nTeams = k;
    l.bFfa = bFfa;
    out.push_back(l);
  };
  add(std::vector<int>(n, 0), PT_("Free for all"), true, PortText("No teams: every player against every other."),
      {PT_("Everyone")});
  if (n < 3)
    return out;
  const int half = n / 2;
  struct Line {
    double (*f)(const TeamPoint &);
    const char *pszName, *pszA, *pszB;
  };
  static const Line kLines[] = {
      {[](const TeamPoint &p) { return p.y; }, PT_("North vs South"), PT_("North"), PT_("South")},
      {[](const TeamPoint &p) { return p.x; }, PT_("West vs East"), PT_("West"), PT_("East")},
      {[](const TeamPoint &p) { return p.x + p.y; }, PT_("North-west vs South-east"), PT_("North-west"),
       PT_("South-east")},
      {[](const TeamPoint &p) { return p.y - p.x; }, PT_("North-east vs South-west"), PT_("North-east"),
       PT_("South-west")},
      {[](const TeamPoint &p) { return -hypot(p.x - 0.5, p.y - 0.5); }, PT_("Edge vs Centre"), PT_("Edge"),
       PT_("Centre")},
  };
  for (const Line &ln : kLines) {
    std::vector<int> order(n);
    for (int i = 0; i < n; ++i)
      order[i] = i;
    std::sort(order.begin(), order.end(), [&](int a, int b) {
      const double fa = ln.f(P[a]), fb = ln.f(P[b]);
      return fa != fb ? fa < fb : a < b;
    });
    std::vector<int> t(n);
    for (int r = 0; r < n; ++r)
      t[order[r]] = r < n - half ? 0 : 1;
    if (Score(P, t) > 0.05)
      add(t, ln.pszName, false, PortText("The starts split along the map into two even sides."), {ln.pszA, ln.pszB});
  }
  for (int k = 2; k <= (std::min)(16, n / 2); ++k) {
    if (n % k)
      continue;
    const std::vector<int> t = Clusters(P, k);
    const int per = n / k;
    bool bCorners = false;
    if (k == 4) {
      std::vector<std::string> nm;
      for (int q = 0; q < 4; ++q)
        nm.push_back(PlaceName(P, t, q));
      bCorners = true;
      for (const char *psz : {"North-west", "North-east", "South-west", "South-east"})
        bCorners = bCorners && std::find(nm.begin(), nm.end(), psz) != nm.end();
    }
    const std::string name = per == 2   ? PT_("Neighbours in pairs")
                             : k == 2   ? PT_("Two sides")
                             : bCorners ? PT_("Four corners")
                                        : Word(k) + " teams of " + std::to_string(per);
    // {0} how many teams, in words, {1} how many starts in each
    const std::string line = per == 2 ? std::string(PortText("Each start with the one nearest it."))
                                      : PortTextF("Starts close together play together: {0} groups of {1}.",
                                                  {PortLower(Word(k)), std::to_string(per)});
    add(t, name.c_str(), false, line, {});
  }
  TeamLayout ffa = out.front();
  out.erase(out.begin());
  // scores compared to three decimals, so a tie keeps the order the layouts were made in, as
  // teams.js sorts them
  std::stable_sort(out.begin(), out.end(), [](const TeamLayout &a, const TeamLayout &b) {
    const long nA = lround(a.fScore * 1000), nB = lround(b.fScore * 1000);
    return a.nTeams != b.nTeams ? a.nTeams < b.nTeams : nA > nB;
  });
  out.erase(std::remove_if(out.begin(), out.end(), [](const TeamLayout &l) { return l.fScore <= 0.08; }), out.end());
  out.insert(out.begin(), ffa);
  return out;
}

int TeamsAdd(std::vector<std::string> *pNames) {
  pNames->push_back("Team " + std::to_string(pNames->size() + 1));
  return static_cast<int>(pNames->size()) - 1;
}

// the teams' own colours, the design's TEAM_BASES
const unsigned kTeamBases[16] = {0xf40404, 0x0c48cc, 0x2cb494, 0x88409c, 0xf88c14, 0x088008, 0xfcfc38, 0xcce0d0,
                                 0x00e4fc, 0xfc6854, 0x10fc18, 0x703014, 0x7cacfc, 0xbca45c, 0xecc4b0, 0x9090b8};

namespace {

void LabOf(unsigned rgb, float *pLab) {
  const float c[3] = {(rgb >> 16 & 0xFF) / 255.0f, (rgb >> 8 & 0xFF) / 255.0f, (rgb & 0xFF) / 255.0f};
  PortRgbToLab(c, pLab);
}

unsigned RgbOf(const float *pRgb) {
  unsigned u = 0;
  for (int i = 0; i < 3; ++i) {
    const long v = lroundf(pRgb[i] * 255.0f);
    u = u << 8 | static_cast<unsigned>(v < 0 ? 0 : v > 255 ? 255 : v);
  }
  return u;
}

// a lightness, chroma and hue as sRGB, the chroma cut until it fits
unsigned Lch(float L, float C, float h) {
  float rgb[3];
  for (int k = 0; k < 24; ++k) {
    const float lab[3] = {L, C * cosf(h), C * sinf(h)};
    if (PortLabToRgb(lab, rgb))
      return RgbOf(rgb);
    C *= 0.9f;
  }
  const float grey[3] = {L, 0.0f, 0.0f};
  PortLabToRgb(grey, rgb);
  return RgbOf(rgb);
}

}  // namespace

std::vector<unsigned> TeamsShades(unsigned base, int n) {
  float o[3];
  LabOf(base, o);
  const float L = o[0], C = hypotf(o[1], o[2]), h = atan2f(o[2], o[1]);
  const float span = (std::min)(0.34f, 0.075f * (n - 1));
  float lo = (std::max)(0.42f, (std::min)(L, 0.86f) - span / 2);
  const float hi = (std::min)(0.9f, lo + span);
  lo = (std::max)(0.36f, hi - span);
  std::vector<unsigned> out;
  for (int i = 0; i < n; ++i) {
    // the base first, then out from it: 0, +1, -1, +2, -2 ... placed on the span
    const int k = i == 0 ? 0 : (i % 2 ? 1 : -1) * ((i + 1) / 2);
    float t = n == 1 ? 0.5f : 0.5f + static_cast<float>(k) / (n - 1);
    t = (std::max)(0.0f, (std::min)(1.0f, t));
    const float Li = n == 1 ? L : lo + (hi - lo) * t, turn = k * 0.06f * (C > 0.03f ? 1.0f : 0.0f);
    out.push_back(i == 0 && n == 1 ? base : Lch(Li, C * (0.85f + 0.15f * (1 - fabsf(t - 0.5f) * 2)), h + turn));
  }
  // lightest first, so a team reads light to dark down its list
  std::stable_sort(out.begin(), out.end(), [](unsigned a, unsigned b) {
    float la[3], lb[3];
    LabOf(a, la);
    LabOf(b, lb);
    return la[0] > lb[0];
  });
  return out;
}
