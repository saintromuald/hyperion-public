// port_clock.cpp -- Debug Tools' game clock: when each game frame, each frame shown
// and each scroll step happened, on the performance counter, to check that Hyperion keeps
// 1.17's pace and how evenly. The live channel's "clock" turns it on, resets it and reads it.
//
// A game frame is due every sgTicksPerFrame_g[gOptions] ms (42 at Fastest), or that over the
// frames a pass runs (a replay's multiple). The readings: the time between game frames against
// that nominal (mean, spread, extremes, a histogram in whole ms), the pace over the run and how
// far the game has drifted from wall time; the time between frames shown and how many were
// shown in each game frame (at 120 Hz and Fastest an even pace is 5 each); the time between
// scroll steps. A pause, or anything that holds the game more than three frames, is a gap: the
// time across it is left out and the drift starts again after it.
#include <windows.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <algorithm>
#include <string>
#include <vector>
#include <SDL3/SDL.h>
#include "port_clock.h"
#include "../render/port_present.h"  // PortFrameDueIn
#include "../platform/port_settings.h"
#include "../platform/port_util.h"  // PortSpeedFrameMs
#include "replay.h"                 // replay_get_initial_game_speed
#include "../../gen/globals117.h"
#include "../platform/port_globals117.h"

namespace {

struct GameTick {
  double fMs;
  float fNominal;
};

bool g_bOn;
LARGE_INTEGER g_start;
std::vector<GameTick> g_game;
std::vector<double> g_shown;
std::vector<double> g_scroll;
const size_t kMost = 1u << 20;
// the pace since the last gap, kept as frames come, for readings every frame (the overlay)
double g_fRunFrom;
double g_fRunNominal;
unsigned g_nRunGaps;

bool IsGap(double d, double nominal, double nominalBefore)
{
  return d > nominal * 3.0 + 20.0 || nominal != nominalBefore;
}

double NowMs(void)
{
  LARGE_INTEGER now, freq;
  QueryPerformanceCounter(&now);
  QueryPerformanceFrequency(&freq);
  return 1000.0 * static_cast<double>(now.QuadPart - g_start.QuadPart) / static_cast<double>(freq.QuadPart);
}

struct Spread {
  size_t n = 0;
  double mean = 0, sd = 0, least = 0, most = 0, p1 = 0, p99 = 0;
};

Spread SpreadOf(std::vector<double> v)
{
  Spread s;
  s.n = v.size();
  if (v.empty())
    return s;
  std::sort(v.begin(), v.end());
  double sum = 0;
  for (double x : v)
    sum += x;
  s.mean = sum / v.size();
  double var = 0;
  for (double x : v)
    var += (x - s.mean) * (x - s.mean);
  s.sd = sqrt(var / v.size());
  s.least = v.front();
  s.most = v.back();
  s.p1 = v[v.size() / 100];
  s.p99 = v[v.size() * 99 / 100];
  return s;
}

std::string SpreadJson(const Spread &s)
{
  char sz[256];
  snprintf(sz, sizeof sz,
           "{\"n\":%u,\"mean\":%.2f,\"sd\":%.2f,\"least\":%.2f,\"most\":%.2f,\"p1\":%.2f,\"p99\":%.2f}",
           static_cast<unsigned>(s.n), s.mean, s.sd, s.least, s.most, s.p1, s.p99);
  return sz;
}

// A histogram in whole ms, as [[ms, count], ...] for the bins that have any.
std::string HistJson(const std::vector<double> &v)
{
  std::vector<unsigned> bins;
  for (double x : v) {
    const size_t b = x < 0 ? 0 : static_cast<size_t>(x + 0.5);
    if (b >= 400)
      continue;
    if (bins.size() <= b)
      bins.resize(b + 1);
    ++bins[b];
  }
  std::string s = "[";
  bool bFirst = true;
  for (size_t i = 0; i < bins.size(); ++i) {
    if (bins[i] == 0)
      continue;
    s += (bFirst ? "[" : ",[") + std::to_string(i) + "," + std::to_string(bins[i]) + "]";
    bFirst = false;
  }
  return s + "]";
}

}  // namespace

// Settings, "Game frame timing" ("frameTiming"): 1 even, the default, the game's frames due
// by the performance counter in whole ms; 0 as 1.17, by GetTickCount, which steps every
// 15.6 ms. Only when a frame runs changes, never what it does. With DLLs injected
// (HYPERION_DLLS) it is 1.17's whatever the setting: the replay harness's observer replaces
// GetTickCount with a clock of its own that runs the game as fast as it can, and logs each
// frame from inside it (tools/replaytest/rt_observer.cpp).
static int g_nFrameTiming = -1;
static int g_nInjected = -1;

int PortFrameTiming(void)
{
  if (g_nInjected < 0)
    g_nInjected = GetEnvironmentVariableA("HYPERION_DLLS", NULL, 0) != 0 ? 1 : 0;
  if (g_nInjected)
    return 0;
  if (g_nFrameTiming < 0) {
    double d = 1.0;
    PortSettingNumber("hyperion", "frameTiming", &d);
    g_nFrameTiming = d == 0.0 ? 0 : 1;
  }
  return g_nFrameTiming;
}

void PortFrameTimingSet(int nTiming)
{
  g_nFrameTiming = nTiming == 0 ? 0 : 1;
}

static ULONG RealTicks(void)
{
  if (PortFrameTiming() == 0)
    return GetTickCount();
  static LARGE_INTEGER s_freq;
  if (s_freq.QuadPart == 0)
    QueryPerformanceFrequency(&s_freq);
  LARGE_INTEGER now;
  QueryPerformanceCounter(&now);
  return static_cast<DWORD>(static_cast<unsigned long long>(now.QuadPart) * 1000ull /
                            static_cast<unsigned long long>(s_freq.QuadPart));
}

// the game's clock ahead of the real one by what the times over 1 have added
static int g_nSpeedTimes = 1;
static ULONG g_nSpeedAhead, g_nSpeedFrom;

ULONG PortGameTicks(void)
{
  const ULONG nReal = RealTicks();
  if (g_nSpeedTimes > 1) {
    g_nSpeedAhead += (nReal - g_nSpeedFrom) * static_cast<ULONG>(g_nSpeedTimes - 1);
    g_nSpeedFrom = nReal;
  }
  return nReal + g_nSpeedAhead;
}

// Settings, "Rest between frames" ("loopRest", on unless 0); HYPERION_LOOP_REST=0 or 1 goes
// before the setting, for a benchmark's runs.
static int g_nRest = -1;

bool PortClockRestOn(void)
{
  if (g_nRest < 0) {
    double d = 1.0;
    if (const char *psz = getenv("HYPERION_LOOP_REST"))
      d = atof(psz);
    else
      PortSettingNumber("hyperion", "loopRest", &d);
    g_nRest = d == 0.0 ? 0 : 1;
  }
  return g_nRest == 1;
}

void PortClockRestSet(bool bOn)
{
  g_nRest = bOn ? 1 : 0;
}

static int g_nBackground = -1;

bool PortClockBackgroundOn(void)
{
  if (g_nBackground < 0) {
    double d = 0.0;
    PortSettingNumber("hyperion", "runInBackground", &d);
    g_nBackground = d == 0.0 ? 0 : 1;
  }
  return g_nBackground == 1;
}

void PortClockBackgroundSet(bool bOn)
{
  g_nBackground = bOn ? 1 : 0;
}

// How late the rest's timer has woken, its last 256 wakes, and the latest of them: the rest
// ends that much before what is due, and 0.2 ms more, so that a wake as late as any of them
// still comes in time. Windows' high-resolution timer wakes about 0.5 ms late, 1.3 ms at the
// latest in 4,000 waits on the developer's machine; a busier machine's wakes come
// later and the margin grows with them, to 2 ms at the most, the margin it had before.
#if defined(_WIN32)
static double g_adLate[256];
static unsigned int g_nLate;  // wakes counted, wrapping harmlessly: only its remainders are read
static double g_dLatest = 0.0018;

static double RestMargin(void)
{
  double d = g_dLatest + 0.0002;
  return d < 0.0005 ? 0.0005 : (d > 0.002 ? 0.002 : d);
}

static void RestLate(double dLate)
{
  const unsigned int n = g_nLate < 256 ? g_nLate + 1 : 256;
  g_adLate[g_nLate++ % 256] = dLate;
  if (dLate > g_dLatest) {
    g_dLatest = dLate;
  } else if (g_nLate % 64 == 0) {
    // the oldest have gone from the window: the latest of those kept
    double d = 0.0;
    for (unsigned int i = 0; i < n; i++)
      d = g_adLate[i] > d ? g_adLate[i] : d;
    g_dLatest = d;
  }
}
#endif

void PortClockRest(long lGameDueMs)
{
#if !defined(__EMSCRIPTEN__)
  PortFrameTiming();  // whether DLLs are injected
  if (g_nInjected || !PortClockRestOn())
    return;
  // the game's clock counts whole ms, so its frame may be due a ms sooner than it says
  double dRest = lGameDueMs > 1 ? (lGameDueMs - 1) / 1000.0 / g_nSpeedTimes : 0.0;
  const double dDraw = PortFrameDueIn();
  if (dDraw >= 0.0 && dDraw < dRest)
    dRest = dDraw;
  // The rest ends a margin before what is due and the loop spins the rest, since a rest to the
  // due time draws frames less evenly; a slice is at most 1 ms. The margin follows the timer's
  // lateness (RestMargin); HYPERION_LOOP_MARGIN_US=<us> fixes it, for measuring.
  static const char *const s_pszMargin = getenv("HYPERION_LOOP_MARGIN_US");
#ifdef _WIN32
  const double dMargin = s_pszMargin != NULL ? atof(s_pszMargin) / 1e6 : RestMargin();
#else
  const double dMargin = s_pszMargin != NULL ? atof(s_pszMargin) / 1e6 : 0.002;
#endif
  dRest -= dMargin;
  if (dRest > 0.001)
    dRest = 0.001;
  if (dRest <= 0.0)
    return;
#ifdef _WIN32
  // a high-resolution timer, as the system's own wait steps every 15.6 ms; a message for the
  // window ends the rest at once. Before Windows 10 1803 there is no such timer, and no rest.
  static const HANDLE s_hTimer =
      CreateWaitableTimerExW(NULL, NULL, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
  if (s_hTimer == NULL)
    return;
  LARGE_INTEGER due;
  due.QuadPart = -static_cast<LONGLONG>(dRest * 1e7);  // from now, in 100 ns
  const Uint64 nFrom = SDL_GetPerformanceCounter();
  if (SetWaitableTimer(s_hTimer, &due, 0, NULL, NULL, FALSE) &&
      MsgWaitForMultipleObjectsEx(1, &s_hTimer, INFINITE, QS_ALLINPUT, MWMO_INPUTAVAILABLE) == WAIT_OBJECT_0)
    RestLate(static_cast<double>(SDL_GetPerformanceCounter() - nFrom) / static_cast<double>(SDL_GetPerformanceFrequency()) -
             dRest);  // the timer's wake, not a message's
#else
  if (dRest >= 0.001)
    SDL_WaitEventTimeout(NULL, 1);  // ended by an event
  else
    SDL_DelayNS(static_cast<Uint64>(dRest * 1e9));  // a sleep: DelayPrecise spins its end
#endif
#else
  (void)lGameDueMs;
#endif
}

void PortGameSpeedTimesSet(int n)
{
  PortGameTicks();  // what the last times added, kept
  g_nSpeedTimes = n < 1 ? 1 : n > 8 ? 8 : n;
  g_nSpeedFrom = RealTicks();
}

int PortGameSpeedTimes(void)
{
  return g_nSpeedTimes;
}

float PortClockNominalMs(void)
{
  const DWORD dwSpeed = gOptions <= 6 ? gOptions : 6;
  const DWORD dwPerPass = sgdwUpdatesPerFrame_g != 0 ? sgdwUpdatesPerFrame_g : 1;
  return static_cast<float>(sgTicksPerFrame_g[dwSpeed]) / static_cast<float>(dwPerPass);
}

unsigned PortClockFrameMs(void)
{
  return PortSpeedFrameMs(gPlayback ? replay_get_initial_game_speed() : static_cast<int>(gOptions));
}

void PortClockGameFrame(void)
{
  if (!g_bOn || g_game.size() >= kMost)
    return;
  const GameTick t = {NowMs(), PortClockNominalMs()};
  if (g_game.empty()) {
    g_fRunFrom = t.fMs;
  } else if (IsGap(t.fMs - g_game.back().fMs, t.fNominal, g_game.back().fNominal)) {
    ++g_nRunGaps;
    g_fRunFrom = t.fMs;
    g_fRunNominal = 0.0;
  } else {
    g_fRunNominal += t.fNominal;
  }
  g_game.push_back(t);
}

void PortClockShown(void)
{
  if (!g_bOn || g_shown.size() >= kMost)
    return;
  g_shown.push_back(NowMs());
}

void PortClockScroll(void)
{
  if (!g_bOn || g_scroll.size() >= kMost)
    return;
  g_scroll.push_back(NowMs());
}

void PortClockStart(void)
{
  g_game.clear();
  g_shown.clear();
  g_scroll.clear();
  g_fRunFrom = g_fRunNominal = 0.0;
  g_nRunGaps = 0;
  QueryPerformanceCounter(&g_start);
  g_bOn = true;
}

void PortClockStop(void)
{
  g_bOn = false;
}

bool PortClockOn(void)
{
  return g_bOn;
}

// The readings so far, as JSON members.
std::string PortClockJson(void)
{
  // the game frames: the time between each and the one before, against its nominal, gaps out
  std::vector<double> gaps, off;
  std::vector<double> recent;
  size_t nGaps = 0;
  double fDriftFrom = g_game.empty() ? 0.0 : g_game.front().fMs;
  double fDriftNominal = 0.0;
  size_t nWithin = 0;
  for (size_t i = 1; i < g_game.size(); ++i) {
    const double d = g_game[i].fMs - g_game[i - 1].fMs;
    const double nominal = g_game[i].fNominal;
    if (IsGap(d, nominal, g_game[i - 1].fNominal)) {
      // a pause, a hold or a change of speed: the drift starts again from here
      ++nGaps;
      fDriftFrom = g_game[i].fMs;
      fDriftNominal = 0.0;
      continue;
    }
    gaps.push_back(d);
    off.push_back(d - nominal);
    fDriftNominal += nominal;
    nWithin += fabs(d - nominal) <= 2.0;
  }
  for (size_t i = gaps.size() > 240 ? gaps.size() - 240 : 0; i < gaps.size(); ++i)
    recent.push_back(gaps[i]);
  const double fNominal = g_game.empty() ? PortClockNominalMs() : g_game.back().fNominal;
  const double fSince = g_game.empty() ? 0.0 : g_game.back().fMs - fDriftFrom;
  const double fDrift = fSince - fDriftNominal;  // more than 0: the game is behind wall time
  const double fPace = fSince > 0.0 ? 1000.0 * (fDriftNominal / fNominal) / fSince : 0.0;
  // the frames shown in each game frame
  std::vector<unsigned> perFrame;
  size_t k = 0;
  for (size_t i = 1; i < g_game.size(); ++i) {
    const double a = g_game[i - 1].fMs, b = g_game[i].fMs;
    if (b - a > g_game[i].fNominal * 3.0 + 20.0)
      continue;
    while (k < g_shown.size() && g_shown[k] < a)
      ++k;
    unsigned n = 0;
    for (size_t j = k; j < g_shown.size() && g_shown[j] < b; ++j)
      ++n;
    if (perFrame.size() <= n)
      perFrame.resize(n + 1);
    ++perFrame[n];
  }
  std::string per = "[";
  bool bFirst = true;
  for (size_t n = 0; n < perFrame.size(); ++n) {
    if (perFrame[n] == 0)
      continue;
    per += (bFirst ? "[" : ",[") + std::to_string(n) + "," + std::to_string(perFrame[n]) + "]";
    bFirst = false;
  }
  per += "]";
  std::vector<double> shownGaps;
  for (size_t i = 1; i < g_shown.size(); ++i) {
    const double d = g_shown[i] - g_shown[i - 1];
    if (d < 250.0)
      shownGaps.push_back(d);
  }
  std::vector<double> scrollGaps;
  for (size_t i = 1; i < g_scroll.size(); ++i) {
    const double d = g_scroll[i] - g_scroll[i - 1];
    if (d < 60.0)
      scrollGaps.push_back(d);
  }
  std::string r = "[";
  for (size_t i = 0; i < recent.size(); ++i) {
    char sz[24];
    snprintf(sz, sizeof sz, "%s%.2f", i ? "," : "", recent[i]);
    r += sz;
  }
  r += "]";
  char sz[512];
  snprintf(sz, sizeof sz,
           "\"on\":%s,\"seconds\":%.1f,\"frame\":%u,\"replay\":%s,\"speed\":%u,\"nominal_ms\":%.2f,"
           "\"nominal_per_s\":%.2f,\"pace_per_s\":%.2f,\"drift_ms\":%.1f,\"gaps\":%u,\"within_2ms_pct\":%.1f,",
           g_bOn ? "true" : "false", g_game.empty() && g_shown.empty() ? 0.0 : NowMs() / 1000.0, oc_dwReplayFrame,
           gPlayback ? "true" : "false", static_cast<unsigned>(gOptions), fNominal, 1000.0 / fNominal, fPace, fDrift,
           static_cast<unsigned>(nGaps), gaps.empty() ? 0.0 : 100.0 * nWithin / gaps.size());
  return std::string(sz) + "\"game_ms\":" + SpreadJson(SpreadOf(gaps)) + ",\"game_hist\":" + HistJson(gaps) +
         ",\"game_off_ms\":" + SpreadJson(SpreadOf(off)) + ",\"recent\":" + r +
         ",\"shown_ms\":" + SpreadJson(SpreadOf(shownGaps)) + ",\"shown_hist\":" + HistJson(shownGaps) +
         ",\"shown_per_game_frame\":" + per + ",\"scroll_ms\":" + SpreadJson(SpreadOf(scrollGaps)) +
         ",\"scroll_hist\":" + HistJson(scrollGaps);
}

// The readings over the last nRecent game frames, for a reading every frame (the overlay):
// the pace and drift since the last gap, the time between game frames and how many frames
// were shown in each; pfGame and pfShown, when given, the last intervals, oldest first.
void PortClockRead(PortClockNow *p, int nRecent, float *pfGame, int nGameMax, float *pfShown, int nShownMax)
{
  *p = PortClockNow();
  p->bOn = g_bOn;
  p->fNominalMs = g_game.empty() ? PortClockNominalMs() : g_game.back().fNominal;
  p->nGaps = g_nRunGaps;
  p->nFrames = static_cast<unsigned>(g_game.size());
  p->fSeconds = g_game.empty() && g_shown.empty() ? 0.0 : NowMs() / 1000.0;
  if (!g_game.empty()) {
    const double fSince = g_game.back().fMs - g_fRunFrom;
    p->fDriftMs = fSince - g_fRunNominal;
    p->fPacePerS = fSince > 0.0 ? 1000.0 * (g_fRunNominal / p->fNominalMs) / fSince : 0.0;
  }
  // the last game frames' intervals, gaps out
  const size_t nFrom = g_game.size() > static_cast<size_t>(nRecent) + 1 ? g_game.size() - nRecent - 1 : 0;
  double sum = 0.0, sum2 = 0.0;
  unsigned n = 0, nWithin = 0;
  unsigned anShown[16] = {};
  size_t k = std::lower_bound(g_shown.begin(), g_shown.end(), nFrom < g_game.size() ? g_game[nFrom].fMs : 0.0) - g_shown.begin();
  p->fLeastMs = 1e9;
  for (size_t i = nFrom + 1; i < g_game.size(); ++i) {
    const double d = g_game[i].fMs - g_game[i - 1].fMs;
    if (IsGap(d, g_game[i].fNominal, g_game[i - 1].fNominal))
      continue;
    sum += d;
    sum2 += d * d;
    ++n;
    nWithin += fabs(d - g_game[i].fNominal) <= 2.0;
    p->fLeastMs = d < p->fLeastMs ? d : p->fLeastMs;
    p->fMostMs = d > p->fMostMs ? d : p->fMostMs;
    while (k < g_shown.size() && g_shown[k] < g_game[i - 1].fMs)
      ++k;
    unsigned nShown = 0;
    while (k < g_shown.size() && g_shown[k] < g_game[i].fMs) {
      ++nShown;
      ++k;
    }
    ++anShown[nShown < 15 ? nShown : 15];
  }
  p->nRecent = n;
  if (n > 0) {
    p->fMeanMs = sum / n;
    p->fSdMs = sqrt(fmax(0.0, sum2 / n - p->fMeanMs * p->fMeanMs));
    p->fWithinPct = 100.0 * nWithin / n;
    for (int i = 0; i < 16; ++i) {
      if (anShown[i] > anShown[p->nShownMost])
        p->nShownMost = i;
      if (anShown[i] != 0) {
        p->nShownLeast = p->nShownLeast < 0 ? i : p->nShownLeast;
        p->nShownMax = i;
      }
    }
  } else {
    p->fLeastMs = 0.0;
  }
  if (pfGame != NULL) {
    int nGot = 0;
    for (size_t i = g_game.size() > static_cast<size_t>(nGameMax) ? g_game.size() - nGameMax : 1; i < g_game.size(); ++i)
      pfGame[nGot++] = static_cast<float>(g_game[i].fMs - g_game[i - 1].fMs);
    p->nGameGot = nGot;
  }
  if (pfShown != NULL) {
    int nGot = 0;
    for (size_t i = g_shown.size() > static_cast<size_t>(nShownMax) ? g_shown.size() - nShownMax : 1; i < g_shown.size(); ++i)
      pfShown[nGot++] = static_cast<float>(g_shown[i] - g_shown[i - 1]);
    p->nShownGot = nGot;
  }
}
