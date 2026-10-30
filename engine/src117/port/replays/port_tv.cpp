// port_tv.cpp -- where the camera looks on its own: following one unit,
// and Replay TV, which finds where a replay's fighting is and looks there
// (docs/modules/replays.md, "Replay TV"). Both only read the game: the camera takes their aim
// (PortViewCamera), and in a replay it never moves the game's own view.
//
// TV scores the map in squares of 12 tiles every few game frames, by hit points and shields
// lost and units gone, every unit warming its square a little and the heat cooling each look;
// it holds a shot 4.5 seconds unless a fight elsewhere is three times as hot, fits the zoom to
// what happened there, and pauses 6 seconds after anything the viewer does. A LAN match watched
// a delay behind reads ahead: the room's copy sends where it was hottest (PortTvHottest), and a
// fight about to come cuts the shot there a little before it starts (PortLanWatchAhead).
#include <windows.h>
#include <math.h>
#include <vector>
#include "port_tv.h"
#include "../render/port_view.h"  // PortViewShape
#include "oc_data.h"
#include "CUnit.h"
#include "CSprite.h"
#include "../../gen/globals117.h"
#include "../lan/port_lanfeed.h"
#include "../platform/port_globals117.h"

namespace {

enum {
  TV_SQUARE = 384,      // a square's side, in map pixels
  TV_EVERY = 4,         // game frames between looks
};

struct Seen {
  bool bOn;
  unsigned short wType;
  int nX, nY;
  int nLife;  // hit points and shields, in 1/256ths
};

bool g_bTv;
double g_fTvPausedUntil;
double g_fTvSwitched;        // when the shot last changed
std::vector<float> g_heat;   // a square's heat
std::vector<Seen> g_seen;    // each unit slot as last looked at
int g_nSquaresX, g_nSquaresY;
unsigned int g_nLookedFrame = 0xFFFFFFFFu;
// the shot: its square, centre and zoom
bool g_bShot;
int g_nShotX, g_nShotY;
double g_fShotX, g_fShotY, g_fShotZoom;
// the hottest place at the last look, a square, and its heat; the last frame something was hit
// or died round it, and the middle of where
int g_nHotX = -1, g_nHotY = -1;
float g_fHot;
unsigned int g_nHotFought = 0xFFFFFFFFu;
int g_nFoughtX, g_nFoughtY;
// this look's events, for the box round the hot square
struct Event {
  int nX, nY;
};
std::vector<Event> g_events;

CUnit *g_pFollow;
unsigned short g_wFollowType;

double Now(void)
{
  LARGE_INTEGER now, freq;
  QueryPerformanceCounter(&now);
  QueryPerformanceFrequency(&freq);
  return static_cast<double>(now.QuadPart) / freq.QuadPart;
}

bool UnitLives(const CUnit *pUnit)
{
  return pUnit->pSprite != NULL && pUnit->lHitPoints > 0;
}

float Around(int sx, int sy)
{
  float f = 0.0f;
  for (int y = sy - 1; y <= sy + 1; ++y)
    for (int x = sx - 1; x <= sx + 1; ++x)
      if (x >= 0 && y >= 0 && x < g_nSquaresX && y < g_nSquaresY)
        f += g_heat[y * g_nSquaresX + x];
  return f;
}

void Look(void)
{
  const int nW = (gwMapWidth * 32 + TV_SQUARE - 1) / TV_SQUARE;
  const int nH = (gwMapHeight * 32 + TV_SQUARE - 1) / TV_SQUARE;
  if (nW != g_nSquaresX || nH != g_nSquaresY) {
    g_nSquaresX = nW;
    g_nSquaresY = nH;
    g_heat.assign(static_cast<size_t>(nW) * nH, 0.0f);
    g_seen.clear();
    g_bShot = false;
  }
  for (float &f : g_heat)
    f *= 0.85f;
  g_events.clear();
  std::vector<bool> here(g_seen.size(), false);
  for (CUnit *pUnit = CUnit::GetUsedList(); pUnit != NULL; pUnit = pUnit->pNext) {
    if (pUnit->bPlayer >= 8 || !UnitLives(pUnit))
      continue;
    const size_t n = static_cast<size_t>(pUnit - CUnit::Head());
    if (n >= g_seen.size()) {
      g_seen.resize(n + 1);
      here.resize(n + 1, false);
    }
    here[n] = true;
    const int x = pUnit->wPositionX, y = pUnit->wPositionY;
    const int sx = x / TV_SQUARE, sy = y / TV_SQUARE;
    if (sx < 0 || sy < 0 || sx >= g_nSquaresX || sy >= g_nSquaresY)
      continue;
    float &heat = g_heat[sy * g_nSquaresX + sx];
    heat += 0.05f;
    const int nLife = pUnit->lHitPoints + static_cast<int>(pUnit->lShields);
    Seen &seen = g_seen[n];
    if (seen.bOn && seen.wType == pUnit->wType && nLife < seen.nLife) {
      heat += 6.0f;
      g_events.push_back({x, y});
    }
    seen.bOn = true;
    seen.wType = pUnit->wType;
    seen.nX = x;
    seen.nY = y;
    seen.nLife = nLife;
  }
  // a unit gone since the last look died there, most likely
  for (size_t n = 0; n < g_seen.size(); ++n) {
    if (!g_seen[n].bOn || here[n])
      continue;
    g_seen[n].bOn = false;
    const int sx = g_seen[n].nX / TV_SQUARE, sy = g_seen[n].nY / TV_SQUARE;
    if (sx >= 0 && sy >= 0 && sx < g_nSquaresX && sy < g_nSquaresY) {
      g_heat[sy * g_nSquaresX + sx] += 8.0f;
      g_events.push_back({g_seen[n].nX, g_seen[n].nY});
    }
  }
  // the hottest place
  int bx = -1, by = -1;
  float fBest = 0.0f;
  for (int y = 0; y < g_nSquaresY; ++y)
    for (int x = 0; x < g_nSquaresX; ++x) {
      const float f = Around(x, y);
      if (f > fBest) {
        fBest = f;
        bx = x;
        by = y;
      }
    }
  g_nHotX = bx;
  g_nHotY = by;
  g_fHot = fBest;
  long long fx = 0, fy = 0;
  int nFought = 0;
  for (const Event &e : g_events)
    if (abs(e.nX / TV_SQUARE - bx) <= 1 && abs(e.nY / TV_SQUARE - by) <= 1) {
      fx += e.nX;
      fy += e.nY;
      ++nFought;
    }
  if (nFought > 0) {
    g_nHotFought = oc_dwReplayFrame;
    g_nFoughtX = static_cast<int>(fx / nFought);
    g_nFoughtY = static_cast<int>(fy / nFought);
  }
  if (bx < 0)
    return;
  const double t = Now();
  const bool bFar = !g_bShot || abs(bx - g_nShotX) > 1 || abs(by - g_nShotY) > 1;
  const float fNow = g_bShot ? Around(g_nShotX, g_nShotY) : 0.0f;
  if (bFar && (!g_bShot || (t - g_fTvSwitched > 4.5 && fBest > fNow * 1.2f) ||
               (t - g_fTvSwitched > 2.0 && fBest > fNow * 3.0f))) {
    g_bShot = true;
    g_nShotX = bx;
    g_nShotY = by;
    g_fTvSwitched = t;
  }
  // the shot's box: what happened round its square, or the square itself
  int x0 = g_nShotX * TV_SQUARE, y0 = g_nShotY * TV_SQUARE;
  int x1 = x0 + TV_SQUARE, y1 = y0 + TV_SQUARE;
  bool bAny = false;
  for (const Event &e : g_events) {
    if (abs(e.nX / TV_SQUARE - g_nShotX) > 1 || abs(e.nY / TV_SQUARE - g_nShotY) > 1)
      continue;
    if (!bAny) {
      x0 = x1 = e.nX;
      y0 = y1 = e.nY;
      bAny = true;
    }
    x0 = e.nX < x0 ? e.nX : x0;
    y0 = e.nY < y0 ? e.nY : y0;
    x1 = e.nX > x1 ? e.nX : x1;
    y1 = e.nY > y1 ? e.nY : y1;
  }
  g_fShotX = (x0 + x1) * 0.5;
  g_fShotY = (y0 + y1) * 0.5;
  // room round it, as much as the view's shape asks
  double w = 0.0, h = 0.0;
  PortViewShape(&w, &h);
  double z = 1.0;
  if (w > 0.0 && h > 0.0) {
    const double zx = (x1 - x0 + 256) / w;
    const double zy = (y1 - y0 + 192) / h;
    z = zx > zy ? zx : zy;
  }
  g_fShotZoom = z < 1.0 ? 1.0 : z > 3.0 ? 3.0 : z;
}

}  // namespace

void PortTvToggle(void)
{
  g_bTv = !g_bTv;
  g_bShot = false;
  g_fTvPausedUntil = 0.0;
  if (g_bTv)
    g_pFollow = NULL;
}

int PortTvOn(void)
{
  return g_bTv ? 1 : 0;
}

void PortTvInput(void)
{
  if (g_bTv)
    g_fTvPausedUntil = Now() + 6.0;
}

int PortTvTarget(double *pfX, double *pfY, double *pfZoom)
{
  // HYPERION_TV=1: on from the start, for a check of a replay's shots
  static int s_nEnv = -1;
  if (s_nEnv < 0) {
    char sz[8];
    s_nEnv = GetEnvironmentVariableA("HYPERION_TV", sz, sizeof sz) > 0 && sz[0] == '1' ? 1 : 0;
    if (s_nEnv)
      g_bTv = true;
  }
  if (!g_bTv || !gPlayback || gwGameMode != 3)
    return 0;
  if (oc_dwReplayFrame != g_nLookedFrame && (oc_dwReplayFrame - g_nLookedFrame >= TV_EVERY ||
                                             oc_dwReplayFrame < g_nLookedFrame)) {
    g_nLookedFrame = oc_dwReplayFrame;
    Look();
  }
  // a LAN match watched: a fight the room's copy has seen coming takes the shot
  int ax, ay;
  float fAhead;
  if (PortLanWatchAhead(&ax, &ay, &fAhead)) {
    const int sx = ax / TV_SQUARE, sy = ay / TV_SQUARE;
    const bool bFar = !g_bShot || abs(sx - g_nShotX) > 1 || abs(sy - g_nShotY) > 1;
    const float fNow = g_bShot ? Around(g_nShotX, g_nShotY) : 0.0f;
    if (bFar && (!g_bShot || (Now() - g_fTvSwitched > 2.0 && fAhead > fNow))) {
      g_bShot = true;
      g_nShotX = sx;
      g_nShotY = sy;
      g_fShotX = ax;
      g_fShotY = ay;
      g_fShotZoom = 1.0;
      g_fTvSwitched = Now();
    }
  }
  if (!g_bShot || Now() < g_fTvPausedUntil)
    return 0;
  *pfX = g_fShotX;
  *pfY = g_fShotY;
  *pfZoom = g_fShotZoom;
  return 1;
}

int PortTvHottest(int *pnX, int *pnY, float *pfHeat)
{
  if (!gPlayback || gwGameMode != 3)
    return 0;
  if (oc_dwReplayFrame != g_nLookedFrame && (oc_dwReplayFrame - g_nLookedFrame >= TV_EVERY ||
                                             oc_dwReplayFrame < g_nLookedFrame)) {
    g_nLookedFrame = oc_dwReplayFrame;
    Look();
  }
  // only a fight: units together are warm, but not news
  if (g_nHotX < 0 || g_nHotFought == 0xFFFFFFFFu || oc_dwReplayFrame - g_nHotFought > 24)
    return 0;
  *pnX = g_nFoughtX;
  *pnY = g_nFoughtY;
  *pfHeat = g_fHot;
  return 1;
}

void PortFollowToggle(void)
{
  if (g_pFollow != NULL) {
    g_pFollow = NULL;
    return;
  }
  CUnit *pUnit = gpStatUnit;
  if (pUnit == NULL || !UnitLives(pUnit))
    return;
  g_pFollow = pUnit;
  g_wFollowType = pUnit->wType;
  g_bTv = false;
}

void PortFollowStop(void)
{
  g_pFollow = NULL;
}

int PortFollowOn(void)
{
  return g_pFollow != NULL ? 1 : 0;
}

int PortFollowAt(double *pfX, double *pfY)
{
  if (g_pFollow == NULL)
    return 0;
  if (gwGameMode != 3 || !UnitLives(g_pFollow) || g_pFollow->wType != g_wFollowType) {
    g_pFollow = NULL;
    return 0;
  }
  *pfX = g_pFollow->pSprite->wPosX;
  *pfY = static_cast<short>(g_pFollow->pSprite->wPosY);
  return 1;
}
