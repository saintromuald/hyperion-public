// port_richbases.cpp -- rich bases, the tenth mutator (port_richbases.h).
//
// The read began as the design's and is in integer maths, so every machine and
// every replay makes the same: resources joined into
// bases, each start's main and natural, a walking distance from every start to every other base,
// and the roll. Distances are in tenths of a tile, shares in thousandths. 0.6.21 left out mineral
// walls, gave a start's main every resource near it, split lines too wide for one base, kept
// naturals near their start, measured the radius in a straight line, rolled at the chance as
// chosen (the user, 2026-10-26: "Why not simplify the formula to a radius formula?") and took
// every start the map has, played or not, so the set-up's picture is the game's and a base fair
// for every start is fair whichever the players get.
#define NOMINMAX
#include <windows.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <algorithm>
#include <mutex>
#include <string>
#include <vector>

#include "port_richbases.h"
#include "port_mutators.h"            // MutatorsOfGame
#include "../platform/port_json.h"    // PortJson
#include "../platform/port_log.h"     // PortLogger
#include "../tools/port_control.h"    // PortControlCommand
#include "../replays/port_snap.h"     // PORT_SNAP_KEEP
#include "../render/port_looks.h"     // PortLooksProvider
#include "../../h/mutators.h"
#include "../../h/net_rand.h"         // net_get_fixed_seed
#include "../../h/playerlimits.h"     // PlayerLobbySlots
#include "../../h/playertables.h"
#include "../../h/poollimits.h"       // POOL_UNITS_STORE, g_pools
#include "../../h/saveload.h"         // pfwrite, pfread, compress, decompress
#include "../../h/TrigAct.h"          // map_msg_default
#include "../../h/CUnit.h"
#include "../../gen/globals117.h"     // gPlayback, gpLoadGameFile, gStartLocations

extern WORD *gfpCellMap;    // 0x54323C, each tile's megatile (gamemap.cpp, convert_cellmap)
extern WORD *sg_vf4_map_g;  // 0x66644C, each megatile's 16 minitiles' flags

namespace {

const PortLogger Log("hyperion_mutators.log");

enum { RICH = 1, RICH_LOAD = 2 };

unsigned char g_unitRich[POOL_UNITS_STORE];  // RICH, RICH_LOAD: each unit's
PORT_SNAP_KEEP_POOL(g_unitRich, g_pools.units);
std::vector<unsigned int> g_chosen;    // the units that turned rich as the game began
std::vector<unsigned int> g_recorded;  // a replay's record of them
bool g_bRecorded;
std::string g_message;                 // what the game's first frame says, until it is said
std::string g_said;                    // and after
std::string g_read = "\"bases\":[]";   // the last read, for the live channel

const int TILE = 32;
const int MINI = 8;  // a minitile, the walk's step
const int HOME = 14 * TILE;  // a resource this near a start is its main's
const int LINK = 9 * TILE;   // two mineral fields within this belong to one line
const int WIDE = 16 * TILE;  // a line wider than this is more than one base's (Diablo's middle)
const int GAS = 16 * TILE;   // a geyser joins the nearest line within this
const int NATURAL = 600;     // a start's natural is within this walk, tenths of a tile
const int WALL = 50;         // a mineral field holding less, at 1.17's share, is a wall
const int NO_WAY = 0x3FFFFFFF;  // no way there on foot

// Farthest only: a base this share of the way from the nearest candidate to the farthest, or
// farther, by the walk from its nearest start.
const int kFarthest = 800;
// Rich bases are fair when each start's way to its nearest one is within this share of every
// other start's (the design's 20%, richread.js).
const int kFair = 200;

struct Resource {
  int x, y;    // its middle, in map pixels
  int nUnit;   // its unit index
  bool bGas;
};

struct Base {
  std::vector<int> res;  // its resources, indices into the read's
  int nFields = 0, nGeysers = 0;
  int cx = 0, cy = 0;    // the middle of its resources
  int mx = 0, my = 0;    // the walkable minitile nearest it, which the distances are taken to
  int nMain = -1;        // the start whose main it is
  bool bNatural = false;
  std::vector<int> dist;        // from each start, tenths of a tile
  std::vector<char> ground;     // and whether on foot
  int nNear = 0, nR = 0, nEven = 0, nChance = 0;
  int nStraight = 0;            // the straight line to the nearest start, map pixels
  bool bEligible = false;
  int nSet = -1;
  bool bRich = false;
};

struct Read {
  int w = 0, h = 0;                         // the map in tiles
  int mw = 0, mh = 0;                       // and in minitiles
  std::vector<unsigned char> walk;          // a minitile walkable
  std::vector<std::pair<int, int>> starts;  // every start of the map, map pixels
  std::vector<Resource> res;
  std::vector<Base> bases;
};

bool IsField(int nType) {
  return nType >= 0xB0 && nType <= 0xB2;
}

bool IsGeyser(int nType) {
  return nType == 0xBC;
}

long long Sq(long long n) {
  return n * n;
}

// The whole square root, exact whatever the floating point does.
long long ISqrt(long long n) {
  long long x = static_cast<long long>(sqrt(static_cast<double>(n)));
  while (x > 0 && x * x > n)
    x--;
  while ((x + 1) * (x + 1) <= n)
    x++;
  return x;
}

// An integer hash of two words, the roll's own, so the game's random numbers are untouched.
DWORD Mix(DWORD a, DWORD b) {
  DWORD h = a * 0x9E3779B1u ^ (b + 0x7F4A7C15u);
  h ^= h >> 16;
  h *= 0x85EBCA6Bu;
  h ^= h >> 13;
  h *= 0xC2B2AE35u;
  h ^= h >> 16;
  return h;
}

// An amount at a mutator's share, as MutatorResourceOf makes it from the map's.
int Scaled(int nAmount, int nShare) {
  if (nShare == 0 || nShare == 100 || nAmount == 0)
    return nAmount;
  const long long ll = (static_cast<long long>(nAmount) * nShare + 50) / 100;
  return static_cast<int>(ll < 1 ? 1 : ll > 0xFFFF ? 0xFFFF : ll);
}

// Each minitile walkable or not, as the tileset's VF4 says and 1.17's regions take it.
void Walkable(Read &r) {
  r.mw = r.w * 4;
  r.mh = r.h * 4;
  r.walk.assign(static_cast<size_t>(r.mw) * r.mh, 0);
  for (int t = 0; t < r.w * r.h; t++) {
    const WORD *pVf4 = &sg_vf4_map_g[(gfpCellMap[t] & 0x7FFF) * 16];
    const int x0 = t % r.w * 4, y0 = t / r.w * 4;
    for (int k = 0; k < 16; k++)
      r.walk[static_cast<size_t>(y0 + k / 4) * r.mw + x0 + k % 4] = pVf4[k] & 1;
  }
}

// The walkable minitile nearest (mx, my), ring by ring, or the minitile itself when none is near.
void NearestWalkable(const Read &r, int *pmx, int *pmy) {
  const int mx = std::min(std::max(*pmx, 0), r.mw - 1), my = std::min(std::max(*pmy, 0), r.mh - 1);
  *pmx = mx;
  *pmy = my;
  for (int d = 0; d <= 64; d++)
    for (int y = my - d; y <= my + d; y++)
      for (int x = mx - d; x <= mx + d; x++) {
        if ((y != my - d && y != my + d && x != mx - d && x != mx + d) || x < 0 || y < 0 || x >= r.mw || y >= r.mh)
          continue;
        if (r.walk[static_cast<size_t>(y) * r.mw + x]) {
          *pmx = x;
          *pmy = y;
          return;
        }
      }
}

// The walking distance from a map place to every minitile, tenths of a minitile: eight ways, 10
// straight and 14 across, never across a corner that is not walkable. Dial's queue: a step is at
// most 14, so sixteen buckets go round.
void Field(const Read &r, int x, int y, std::vector<int> *pd) {
  std::vector<int> &d = *pd;
  d.assign(static_cast<size_t>(r.mw) * r.mh, NO_WAY);
  int sx = x / MINI, sy = y / MINI;
  NearestWalkable(r, &sx, &sy);
  const int s = sy * r.mw + sx;
  if (!r.walk[s])
    return;
  std::vector<int> q[16];
  d[s] = 0;
  q[0].push_back(s);
  size_t nPending = 1;
  std::vector<int> list;
  for (int cur = 0; nPending != 0; cur++) {
    list.clear();
    list.swap(q[cur & 15]);
    for (int t : list) {
      nPending--;
      if (d[t] != cur)
        continue;
      const int tx = t % r.mw, ty = t / r.mw;
      for (int dy = -1; dy <= 1; dy++)
        for (int dx = -1; dx <= 1; dx++) {
          const int nx = tx + dx, ny = ty + dy;
          if ((dx == 0 && dy == 0) || nx < 0 || ny < 0 || nx >= r.mw || ny >= r.mh)
            continue;
          const int n = ny * r.mw + nx;
          if (!r.walk[n] || (dx != 0 && dy != 0 && (!r.walk[ty * r.mw + nx] || !r.walk[ny * r.mw + tx])))
            continue;
          const int nd = cur + (dx != 0 && dy != 0 ? 14 : 10);
          if (nd < d[n]) {
            d[n] = nd;
            q[nd & 15].push_back(n);
            nPending++;
          }
        }
    }
  }
}

// The map's resources but its walls, in the order of their places, and every start the map has.
void Gather(Read &r) {
  const int nWall = Scaled(WALL, MutatorsOfGame().nMinerals);
  for (CUnit *p = CUnit::GetUsedList(); p != NULL; p = p->pNext) {
    if (!IsField(p->wType) && !IsGeyser(p->wType))
      continue;
    if (IsField(p->wType) && p->wResourceAmount < nWall)
      continue;
    Resource res = {p->wPositionX, p->wPositionY, static_cast<int>(p - CUnit::Head()), IsGeyser(p->wType)};
    r.res.push_back(res);
  }
  std::sort(r.res.begin(), r.res.end(), [](const Resource &a, const Resource &b) {
    return a.y != b.y ? a.y < b.y : a.x != b.x ? a.x < b.x : a.nUnit < b.nUnit;
  });
  for (int s = 0; s < PlayerLobbySlots(); s++) {
    const point16_t &pt = gStartLocations[s];
    if (pt.x != 0 || pt.y != 0)
      r.starts.push_back(std::make_pair(static_cast<int>(pt.x), static_cast<int>(pt.y)));
  }
}

// A line of fields wider than WIDE as two, from its two fields farthest apart, each field to the
// nearer middle until none moves; each part again while it is too wide.
void Split(const Read &r, const std::vector<int> &fields, std::vector<std::vector<int>> *pOut) {
  size_t a = 0, b = 0;
  long long llFar = -1;
  for (size_t i = 0; i < fields.size(); i++)
    for (size_t j = i + 1; j < fields.size(); j++) {
      const long long ll = Sq(r.res[fields[i]].x - r.res[fields[j]].x) + Sq(r.res[fields[i]].y - r.res[fields[j]].y);
      if (ll > llFar) {
        llFar = ll;
        a = i;
        b = j;
      }
    }
  if (llFar <= Sq(WIDE)) {
    pOut->push_back(fields);
    return;
  }
  long long ax = r.res[fields[a]].x, ay = r.res[fields[a]].y, bx = r.res[fields[b]].x, by = r.res[fields[b]].y;
  std::vector<int> side(fields.size(), -1);
  for (int nPass = 0; nPass < 32; nPass++) {
    bool bMoved = false;
    long long sx[2] = {0, 0}, sy[2] = {0, 0};
    long long n[2] = {0, 0};
    for (size_t i = 0; i < fields.size(); i++) {
      const Resource &f = r.res[fields[i]];
      const int c = Sq(f.x - bx) + Sq(f.y - by) < Sq(f.x - ax) + Sq(f.y - ay) ? 1 : 0;
      bMoved = bMoved || c != side[i];
      side[i] = c;
      sx[c] += f.x;
      sy[c] += f.y;
      n[c]++;
    }
    if (!bMoved || n[0] == 0 || n[1] == 0)
      break;
    ax = sx[0] / n[0];
    ay = sy[0] / n[0];
    bx = sx[1] / n[1];
    by = sy[1] / n[1];
  }
  std::vector<int> half[2];
  for (size_t i = 0; i < fields.size(); i++)
    half[side[i]].push_back(fields[i]);
  if (half[0].empty() || half[1].empty()) {
    pOut->push_back(fields);
    return;
  }
  Split(r, half[0], pOut);
  Split(r, half[1], pOut);
}

// Bases: every resource near a start its main's; the other fields in lines by single linkage, a
// line too wide split; each other geyser to the line with the nearest field within reach. A group
// of fewer than four fields and no geyser is no base, unless it is a start's.
void Cluster(Read &r) {
  const int nStarts = static_cast<int>(r.starts.size());
  std::vector<Base> groups(nStarts);
  for (int s = 0; s < nStarts; s++)
    groups[s].nMain = s;
  std::vector<int> fields, geysers;
  for (int i = 0; i < static_cast<int>(r.res.size()); i++) {
    const Resource &res = r.res[i];
    int nHome = -1;
    long long llBest = Sq(HOME) + 1;
    for (int s = 0; s < nStarts; s++) {
      const long long ll = Sq(res.x - r.starts[s].first) + Sq(res.y - r.starts[s].second);
      if (ll < llBest) {
        llBest = ll;
        nHome = s;
      }
    }
    if (nHome >= 0) {
      groups[nHome].res.push_back(i);
      (res.bGas ? groups[nHome].nGeysers : groups[nHome].nFields)++;
    } else {
      (res.bGas ? geysers : fields).push_back(i);
    }
  }
  std::vector<int> parent(fields.size());
  for (size_t i = 0; i < parent.size(); i++)
    parent[i] = static_cast<int>(i);
  auto root = [&parent](int i) {
    while (parent[i] != i)
      i = parent[i] = parent[parent[i]];
    return i;
  };
  for (size_t a = 0; a < fields.size(); a++)
    for (size_t b = a + 1; b < fields.size(); b++) {
      const Resource &ra = r.res[fields[a]], &rb = r.res[fields[b]];
      if (Sq(ra.x - rb.x) + Sq(ra.y - rb.y) <= Sq(LINK))
        parent[root(static_cast<int>(a))] = root(static_cast<int>(b));
    }
  std::vector<std::vector<int>> lines;
  std::vector<int> lineOf(fields.size(), -1);
  for (size_t i = 0; i < fields.size(); i++) {
    const int k = root(static_cast<int>(i));
    if (lineOf[k] < 0) {
      lineOf[k] = static_cast<int>(lines.size());
      lines.push_back(std::vector<int>());
    }
    lines[lineOf[k]].push_back(fields[i]);
  }
  std::vector<std::vector<int>> parts;
  for (const std::vector<int> &line : lines)
    Split(r, line, &parts);
  for (const std::vector<int> &part : parts) {
    Base b;
    b.res = part;
    b.nFields = static_cast<int>(part.size());
    groups.push_back(b);
  }
  for (int g : geysers) {
    const Resource &rg = r.res[g];
    int nBest = -1;
    long long llBest = Sq(GAS) + 1;
    for (int k = nStarts; k < static_cast<int>(groups.size()); k++)
      for (int i : groups[k].res) {
        const long long ll = Sq(r.res[i].x - rg.x) + Sq(r.res[i].y - rg.y);
        if (!r.res[i].bGas && ll < llBest) {
          llBest = ll;
          nBest = k;
        }
      }
    if (nBest < 0) {
      nBest = static_cast<int>(groups.size());
      groups.push_back(Base());
    }
    groups[nBest].res.push_back(g);
    groups[nBest].nGeysers++;
  }
  for (Base &b : groups) {
    if (b.res.empty() || (b.nMain < 0 && b.nFields < 4 && b.nGeysers == 0))
      continue;
    long long sx = 0, sy = 0;
    for (int i : b.res) {
      sx += r.res[i].x;
      sy += r.res[i].y;
    }
    b.cx = static_cast<int>(sx / static_cast<long long>(b.res.size()));
    b.cy = static_cast<int>(sy / static_cast<long long>(b.res.size()));
    b.mx = b.cx / MINI;
    b.my = b.cy / MINI;
    NearestWalkable(r, &b.mx, &b.my);
    r.bases.push_back(b);
  }
  std::sort(r.bases.begin(), r.bases.end(), [](const Base &a, const Base &b) {
    return a.cy != b.cy ? a.cy < b.cy : a.cx < b.cx;
  });
}

// The walk from every start to every base, and each start's natural: its nearest other base with a
// mineral line within NATURAL on foot, nearer it than any other start; by air from an island start
// with none on foot.
void Distances(Read &r) {
  const int nStarts = static_cast<int>(r.starts.size());
  std::vector<int> field;
  for (int s = 0; s < nStarts; s++) {
    Field(r, r.starts[s].first, r.starts[s].second, &field);
    for (Base &b : r.bases) {
      const int d = field[static_cast<size_t>(b.my) * r.mw + b.mx];
      b.ground.push_back(d < NO_WAY ? 1 : 0);
      if (d < NO_WAY) {
        b.dist.push_back(d * MINI / TILE);
      } else {
        // an island: the straight line, as a drop goes
        const long long ll = Sq(b.mx * MINI + MINI / 2 - r.starts[s].first) + Sq(b.my * MINI + MINI / 2 - r.starts[s].second);
        b.dist.push_back(static_cast<int>(ISqrt(ll * 100) / TILE));
      }
    }
  }
  for (int s = 0; s < nStarts; s++) {
    bool bGround = false;
    for (const Base &b : r.bases)
      bGround = bGround || (b.nMain < 0 && b.nFields >= 4 && b.ground[s]);
    int nBest = -1;
    for (int k = 0; k < static_cast<int>(r.bases.size()); k++) {
      const Base &b = r.bases[k];
      if (b.nMain >= 0 || b.nFields < 4 || b.dist[s] > NATURAL || (bGround && !b.ground[s]))
        continue;
      bool bNearest = true;
      for (int o = 0; o < nStarts; o++)
        bNearest = bNearest && (o == s || b.dist[o] > b.dist[s]);
      if (bNearest && (nBest < 0 || b.dist[s] < r.bases[nBest].dist[s]))
        nBest = k;
    }
    if (nBest >= 0)
      r.bases[nBest].bNatural = true;
  }
}

// How far apart the starts' ways to their nearest of these bases are, a share of the longest.
int Spread(const Read &r, const std::vector<int> &bases) {
  int nLo = NO_WAY, nHi = 0;
  for (size_t s = 0; s < r.starts.size(); s++) {
    int n = NO_WAY;
    for (int k : bases)
      n = std::min(n, r.bases[k].dist[s]);
    nLo = std::min(nLo, n);
    nHi = std::max(nHi, n);
  }
  return nHi > 0 ? static_cast<int>(static_cast<long long>(nHi - nLo) * 1000 / nHi) : 0;
}

// A set of bases rolled once, its chance in thousandths, and whether it is fair.
struct Set {
  std::vector<int> bases;
  int nChance = 0, nR = 0;
  bool bFair = true;
};

// The candidates, the ones the choice makes eligible at its chance, and the sets they make. The
// reason none can turn rich, or "".
std::string Sets(Read &r, const MutatorSet &m, std::vector<Set> *pSets) {
  std::vector<int> cand;
  for (int k = 0; k < static_cast<int>(r.bases.size()); k++)
    if (r.bases[k].nMain < 0 && !r.bases[k].bNatural && !r.bases[k].dist.empty())
      cand.push_back(k);
  if (cand.empty())
    return "Every base on this map is a start's main or natural.";
  int nNearMin = NO_WAY, nNearMax = 0;
  for (int k : cand) {
    Base &b = r.bases[k];
    int nFar = 0;
    b.nNear = NO_WAY;
    b.nStraight = NO_WAY;
    for (size_t s = 0; s < r.starts.size(); s++) {
      b.nNear = std::min(b.nNear, b.dist[s]);
      nFar = std::max(nFar, b.dist[s]);
      const long long ll = Sq(b.cx - r.starts[s].first) + Sq(b.cy - r.starts[s].second);
      b.nStraight = std::min(b.nStraight, static_cast<int>(ISqrt(ll)));
    }
    b.nEven = nFar > 0 ? (nFar - b.nNear) * 1000 / nFar : 0;
    nNearMin = std::min(nNearMin, b.nNear);
    nNearMax = std::max(nNearMax, b.nNear);
  }
  const int nRadius = MutatorRichRadiusTiles(m, r.w, r.h);
  bool bAny = false;
  for (int k : cand) {
    Base &b = r.bases[k];
    b.nR = nNearMax > nNearMin ? (b.nNear - nNearMin) * 1000 / (nNearMax - nNearMin) : 1000;
    b.bEligible = m.nRich == MUTATOR_RICH_NATURALS || (m.nRich == MUTATOR_RICH_RADIUS && b.nStraight >= nRadius * TILE) ||
                  (m.nRich == MUTATOR_RICH_FAR && b.nR >= kFarthest);
    b.nChance = b.bEligible ? m.nRichChance * 10 : 0;
    bAny = bAny || b.bEligible;
  }
  if (!bAny && m.nRich == MUTATOR_RICH_RADIUS)
    return "No base is " + std::to_string(nRadius) + " tiles or more from every start.";
  if (!bAny)
    return "No base on this map is far enough from every start.";
  // the sets, each rolled once: a base and the fewest others that make it fair with Fair on (each
  // start's way to its nearest rich base within kFair of the others'), or each base alone. Rich
  // sets that are each fair are fair together. A base no set makes fair still rolls, alone: being
  // far enough from every start is what makes a base eligible, and the middle of a map whose
  // starts ring its edge, nearer some than others, is fair game (the user, 2026-10-26, on Big
  // Game Hunters 3x3, where no set of its 93 bases came within kFair).
  std::vector<Set> &sets = *pSets;
  const bool bFair = (m.nRichRules & MUTATOR_RICH_FAIR) != 0;
  for (int k : cand) {
    if (!r.bases[k].bEligible || r.bases[k].nSet >= 0)
      continue;
    Set s;
    s.bases.push_back(k);
    int nSpread = Spread(r, s.bases);
    while (bFair && nSpread > kFair) {
      int nBest = -1, nBestSpread = nSpread;
      for (int j : cand) {
        if (!r.bases[j].bEligible || r.bases[j].nSet >= 0 || std::find(s.bases.begin(), s.bases.end(), j) != s.bases.end())
          continue;
        s.bases.push_back(j);
        const int n = Spread(r, s.bases);
        s.bases.pop_back();
        if (n < nBestSpread) {
          nBestSpread = n;
          nBest = j;
        }
      }
      if (nBest < 0)
        break;
      s.bases.push_back(nBest);
      nSpread = nBestSpread;
    }
    s.bFair = !bFair || nSpread <= kFair;
    if (!s.bFair)
      s.bases.resize(1);  // the others stay free for a set of their own
    for (int j : s.bases)
      r.bases[j].nSet = static_cast<int>(sets.size());
    sets.push_back(s);
  }
  // a base unfair alone joins the fair set it leaves fairest, if that stays fair: Diablo's middle,
  // two bases each nearer some starts than others, is fair together
  for (size_t i = 0; bFair && i < sets.size(); i++) {
    if (sets[i].bFair)
      continue;
    int nBest = -1, nBestSpread = kFair + 1;
    for (size_t j = 0; j < sets.size(); j++) {
      if (!sets[j].bFair)
        continue;
      std::vector<int> both = sets[j].bases;
      both.push_back(sets[i].bases[0]);
      const int n = Spread(r, both);
      if (n < nBestSpread) {
        nBestSpread = n;
        nBest = static_cast<int>(j);
      }
    }
    if (nBest >= 0) {
      sets[nBest].bases.push_back(sets[i].bases[0]);
      sets[i].bases.clear();
    }
  }
  // the sets left, numbered again, each with its bases' chance and share
  std::vector<Set> kept;
  for (Set &s : sets) {
    if (s.bases.empty())
      continue;
    for (int j : s.bases) {
      r.bases[j].nSet = static_cast<int>(kept.size());
      s.nChance += r.bases[j].nChance;
      s.nR += r.bases[j].nR;
    }
    s.nChance /= static_cast<int>(s.bases.size());
    s.nR /= static_cast<int>(s.bases.size());
    kept.push_back(s);
  }
  sets.swap(kept);
  return "";
}

// The sets and the roll. The reason none turned rich, or "".
std::string Roll(Read &r, const MutatorSet &m, DWORD dwSeed) {
  std::vector<Set> sets;
  const std::string why = Sets(r, m, &sets);
  if (!why.empty())
    return why;
  int nRich = 0;
  for (const Set &s : sets) {
    if (Mix(dwSeed, static_cast<DWORD>(s.bases[0])) % 1000 >= static_cast<DWORD>(s.nChance))
      continue;
    for (int k : s.bases)
      r.bases[k].bRich = true;
    nRich++;
  }
  if (nRich == 0 && (m.nRichRules & MUTATOR_RICH_AT_LEAST_ONE) != 0) {
    int nBest = -1;
    for (int i = 0; i < static_cast<int>(sets.size()); i++)
      if (nBest < 0 || sets[i].nR > sets[nBest].nR)
        nBest = i;
    for (int k : sets[nBest].bases)
      r.bases[k].bRich = true;
    nRich++;
  }
  return nRich == 0 ? "None turned rich this game." : "";
}

// A seed of the map's own: its resources' places and kinds, the same in every game on it.
DWORD MapSeed(const Read &r) {
  DWORD dw = 0x52494348u;  // "RICH"
  for (const Resource &res : r.res)
    dw = Mix(dw, static_cast<DWORD>(res.x) | static_cast<DWORD>(res.y) << 16 | (res.bGas ? 0x80000000u : 0));
  return dw;
}

std::string Ints(const std::vector<int> &v) {
  std::string s;
  for (int n : v)
    s += (s.empty() ? "" : ",") + std::to_string(n);
  return "[" + s + "]";
}

void Json(const Read &r) {
  std::string s;
  for (size_t k = 0; k < r.bases.size(); k++) {
    const Base &b = r.bases[k];
    std::vector<int> units;
    for (int i : b.res)
      units.push_back(r.res[i].nUnit);
    s += std::string(s.empty() ? "" : ",") + "{\"id\":" + std::to_string(k) + ",\"x\":" + std::to_string(b.cx) +
         ",\"y\":" + std::to_string(b.cy) + ",\"fields\":" + std::to_string(b.nFields) + ",\"geysers\":" +
         std::to_string(b.nGeysers) + ",\"kind\":\"" +
         (b.nMain >= 0 ? "main" : b.bNatural ? "natural" : "far") + "\",\"main\":" + std::to_string(b.nMain) +
         ",\"at\":[" + std::to_string(b.mx * MINI + MINI / 2) + "," + std::to_string(b.my * MINI + MINI / 2) +
         "],\"dist\":" + Ints(b.dist) + ",\"ground\":" + Ints(std::vector<int>(b.ground.begin(), b.ground.end())) +
         ",\"near\":" + std::to_string(b.nNear) + ",\"straight\":" + std::to_string(b.nStraight / TILE) + ",\"r\":" + std::to_string(b.nR) +
         ",\"even\":" + std::to_string(b.nEven) + ",\"eligible\":" + (b.bEligible ? "true" : "false") +
         ",\"chance\":" + std::to_string(b.nChance) + ",\"set\":" + std::to_string(b.nSet) + ",\"rich\":" +
         (b.bRich ? "true" : "false") + ",\"units\":" + Ints(units) + "}";
  }
  std::string starts;
  for (const auto &st : r.starts)
    starts += (starts.empty() ? "[" : ",[") + std::to_string(st.first) + "," + std::to_string(st.second) + "]";
  g_read = "\"starts\":[" + starts + "],\"bases\":[" + s + "]";
}

void Mark(int nUnit) {
  if (nUnit >= 0 && nUnit < g_pools.units) {
    g_unitRich[nUnit] |= RICH;
    g_chosen.push_back(static_cast<unsigned int>(nUnit));
  }
}

std::string Said(int nBases) {
  return "Rich resources: " + std::to_string(nBases) + (nBases == 1 ? " base turned rich." : " bases turned rich.");
}

// The looks of rich bases (port_looks.h): a rich mineral field gold, a rich geyser and the
// building on it violet, and a worker's rich load as its kind.
int RichLookOf(int nUnit, const CUnit *pUnit) {
  if (!RichBasesInGame() || nUnit < 0 || nUnit >= g_pools.units || g_unitRich[nUnit] == 0)
    return 0;
  static const int s_nGold = PortLookFind("rich-gold"), s_nViolet = PortLookFind("rich-violet");
  // a geyser run dry is drawn as 1.17 draws it, depleted
  if (g_unitRich[nUnit] & RICH)
    return IsField(pUnit->wType) ? s_nGold : pUnit->wResourceAmount != 0 ? s_nViolet : 0;
  if (pUnit->bCarryResource == 0)
    return 0;
  return pUnit->bCarryResource == 2 ? s_nGold : s_nViolet;
}

// richbases: the game's read, its choice and the units that turned rich.
bool CmdRichBases(const PortJson &, std::string *pAnswer) {
  const MutatorSet &m = MutatorsOfGame();
  std::string units;
  for (unsigned int n : g_chosen)
    units += (units.empty() ? "" : ",") + std::to_string(n);
  *pAnswer = "\"on\":" + std::string(RichBasesInGame() ? "true" : "false") + ",\"where\":" +
             std::to_string(m.nRich) + ",\"radius\":" + std::to_string(m.nRichRadius) + ",\"chance\":" +
             std::to_string(m.nRichChance) + ",\"rules\":" +
             std::to_string(m.nRichRules) + ",\"message\":" + PortJsonQuote(g_message.empty() ? g_said : g_message) + ",\"rich\":[" + units +
             "]," + g_read;
  return true;
}

}  // namespace

bool RichBasesInGame(void) {
  return g_hxMutatorsOn && MutatorsOfGame().nRich != MUTATOR_RICH_OFF;
}

void RichBasesMapLoaded(void) {
  static bool s_bCommand = false;
  if (!s_bCommand) {
    s_bCommand = true;
    PortControlCommand("richbases", CmdRichBases);
    PortLooksProvider(RichLookOf);
  }
  memset(g_unitRich, 0, sizeof g_unitRich);
  g_chosen.clear();
  g_message.clear();
  g_said.clear();
  g_read = "\"bases\":[]";
  // a save's come with it (HXST, RichBasesLoad)
  if (!RichBasesInGame() || gpLoadGameFile != NULL)
    return;
  const MutatorSet &m = MutatorsOfGame();
  if (gPlayback != 0 && g_bRecorded) {
    // the replay's own choice, whatever this build's read would make
    for (unsigned int n : g_recorded)
      Mark(static_cast<int>(n));
    g_message = "Rich resources: " + std::to_string(g_chosen.size()) + " mineral fields and geysers are rich.";
    Log("rich bases from the replay: %d resources", static_cast<int>(g_chosen.size()));
    return;
  }
  if (m.nRich == MUTATOR_RICH_EVERY) {
    // a mineral wall stays as it is
    const int nWall = Scaled(WALL, m.nMinerals);
    for (CUnit *p = CUnit::GetUsedList(); p != NULL; p = p->pNext)
      if ((IsField(p->wType) && p->wResourceAmount >= nWall) || IsGeyser(p->wType))
        Mark(static_cast<int>(p - CUnit::Head()));
    std::sort(g_chosen.begin(), g_chosen.end());
    g_message = "Rich resources: every mineral field and geyser is rich.";
    Log("rich bases, every one: %d resources", static_cast<int>(g_chosen.size()));
    return;
  }
  const DWORD dwStart = GetTickCount();
  Read r;
  r.w = gwMapWidth;
  r.h = gwMapHeight;
  if (gfpCellMap == NULL || sg_vf4_map_g == NULL || r.w <= 0 || r.h <= 0)
    return;
  Walkable(r);
  Gather(r);
  Cluster(r);
  Distances(r);
  const DWORD dwSeed = (m.nRichRules & MUTATOR_RICH_SAME_MAP) != 0 ? MapSeed(r) : static_cast<DWORD>(net_get_fixed_seed());
  const std::string why = Roll(r, m, dwSeed);
  int nBases = 0;
  for (const Base &b : r.bases) {
    if (!b.bRich)
      continue;
    nBases++;
    for (int i : b.res)
      Mark(r.res[i].nUnit);
  }
  std::sort(g_chosen.begin(), g_chosen.end());
  Json(r);
  g_message = why.empty() ? Said(nBases) : "Rich resources: " + why;
  Log("rich bases on a %dx%d map, %d starts, %d bases, where %d, radius %d, chance %d%%, rules %d, seed %#lx: %s (%d "
      "resources, %lu ms)", r.w, r.h, static_cast<int>(r.starts.size()), static_cast<int>(r.bases.size()), m.nRich,
      m.nRichRadius, m.nRichChance, m.nRichRules,
      static_cast<unsigned long>(dwSeed), g_message.c_str(),
      static_cast<int>(g_chosen.size()), static_cast<unsigned long>(GetTickCount() - dwStart));
}

std::string RichBasesPreview(int nMapW, int nMapH, const std::vector<unsigned char> &walk,
                             const std::vector<std::pair<int, int>> &starts, const std::vector<RichBaseResource> &res,
                             const MutatorSet &m, std::vector<RichBasePreview> *pOut) {
  pOut->clear();
  Read r;
  r.w = nMapW;
  r.h = nMapH;
  r.mw = r.w * 4;
  r.mh = r.h * 4;
  if (r.w <= 0 || r.h <= 0 || walk.size() != static_cast<size_t>(r.mw) * r.mh)
    return "";
  r.walk = walk;
  r.starts = starts;
  const int nWall = Scaled(WALL, m.nMinerals);
  for (size_t i = 0; i < res.size(); i++) {
    if (!res[i].bGas && Scaled(std::min(res[i].nAmount, 0xFFFF), m.nMinerals) < nWall)
      continue;
    Resource one = {res[i].x, res[i].y, static_cast<int>(i), res[i].bGas};
    r.res.push_back(one);
  }
  std::sort(r.res.begin(), r.res.end(), [](const Resource &a, const Resource &b) {
    return a.y != b.y ? a.y < b.y : a.x != b.x ? a.x < b.x : a.nUnit < b.nUnit;
  });
  // the bases and the walks to them are the map's and its walls' alone, kept for the next choice
  // on the same map: on a large one they take most of a second (a 384-tile map's sixteen starts),
  // and only the sets differ (the user, 2026-10-26: the set-up stalled as each choice was read).
  // The set-up reads on a thread of its own (port_front_mutators.cpp), so under a lock.
  static std::mutex s_lock;
  static std::string s_key;
  static Read s_read;
  unsigned long long qwWalk = 0xCBF29CE484222325ull;
  for (unsigned char b : r.walk)
    qwWalk = (qwWalk ^ b) * 0x100000001B3ull;
  std::string key = std::to_string(r.w) + "x" + std::to_string(r.h) + "/" + std::to_string(qwWalk) + "/";
  for (const std::pair<int, int> &s : r.starts)
    key += std::to_string(s.first) + "," + std::to_string(s.second) + ";";
  key += "/";
  for (const Resource &one : r.res)
    key += std::to_string(one.x) + "," + std::to_string(one.y) + (one.bGas ? "g" : "f") + std::to_string(one.nUnit) + ";";
  {
    std::lock_guard<std::mutex> hold(s_lock);
    if (key == s_key) {
      r = s_read;
    } else {
      Cluster(r);
      Distances(r);
      s_key = key;
      s_read = r;
    }
  }
  std::string why;
  std::vector<Set> sets;
  if (m.nRich != MUTATOR_RICH_EVERY && m.nRich != MUTATOR_RICH_OFF)
    why = Sets(r, m, &sets);
  for (const Base &b : r.bases) {
    RichBasePreview one = {b.cx, b.cy, b.nMain >= 0 ? 0 : b.bNatural ? 1 : 2, 0};
    if (m.nRich == MUTATOR_RICH_EVERY)
      one.nChance = 1000;
    else if (b.nSet >= 0 && b.nSet < static_cast<int>(sets.size()))
      one.nChance = sets[b.nSet].nChance;
    pOut->push_back(one);
  }
  return why;
}

void RichBasesFrame(void) {
  if (g_message.empty())
    return;
  // the host may have the start say nothing; the live channel still has it
  if ((MutatorsOfGame().nRichRules & MUTATOR_RICH_QUIET) == 0)
    map_msg_default(g_message.c_str(), 8000);
  g_said = g_message;
  g_message.clear();
}

void MutatorUnitMade(int nUnit) {
  if (nUnit >= 0 && nUnit < g_pools.units)
    g_unitRich[nUnit] = 0;
}

bool RichBasesResource(int nUnit) {
  return nUnit >= 0 && nUnit < g_pools.units && (g_unitRich[nUnit] & RICH) != 0;
}

bool RichBasesCarry(int nUnit) {
  return nUnit >= 0 && nUnit < g_pools.units && (g_unitRich[nUnit] & RICH_LOAD) != 0;
}

const std::vector<unsigned int> &RichBasesChosen(void) {
  return g_chosen;
}

int MutatorTripOf(int nUnit, bool bGas) {
  if (!RichBasesResource(nUnit))
    return 8;
  return bGas ? MUTATOR_RICH_GAS_TRIP : MUTATOR_RICH_MINERAL_TRIP;
}

const char *MutatorRichNameOf(int nUnit, int nType) {
  if (!RichBasesResource(nUnit))
    return NULL;
  return IsField(nType) ? "Rich Mineral Field" : IsGeyser(nType) ? "Rich Vespene Geyser" : NULL;
}

void MutatorCarryOf(int nWorker, int nSource) {
  if (nWorker < 0 || nWorker >= g_pools.units)
    return;
  if (RichBasesResource(nSource))
    g_unitRich[nWorker] |= RICH_LOAD;
  else
    g_unitRich[nWorker] &= ~RICH_LOAD;
}

void RichBasesRecord(std::vector<unsigned int> *pOut) {
  *pOut = g_chosen;
}

void RichBasesRecorded(const std::vector<unsigned int> *pList) {
  g_bRecorded = pList != NULL;
  g_recorded = pList != NULL ? *pList : std::vector<unsigned int>();
}

int RichBasesSave(void *pFile) {
  const DWORD dwCount = static_cast<DWORD>(g_chosen.size());
  return pfwrite(&dwCount, 4, pFile) && (dwCount == 0 || pfwrite(&g_chosen[0], static_cast<int>(dwCount * 4), pFile))
      && compress(g_unitRich, g_pools.units, pFile);
}

int RichBasesLoad(void *pFile) {
  DWORD dwCount = 0;
  if (!pfread(&dwCount, 4, pFile) || dwCount > static_cast<DWORD>(g_pools.units))
    return 0;
  g_chosen.assign(dwCount, 0);
  if (dwCount != 0 && !pfread(&g_chosen[0], static_cast<int>(dwCount * 4), pFile))
    return 0;
  memset(g_unitRich, 0, sizeof g_unitRich);
  return decompress(g_unitRich, g_pools.units, pFile);
}

const unsigned char *RichBasesTable(void) {
  return RichBasesInGame() ? g_unitRich : NULL;
}
