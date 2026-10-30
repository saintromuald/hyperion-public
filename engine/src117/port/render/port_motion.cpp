// port_motion.cpp -- the picture's motion (port_motion.h): each channel a move from one value
// to another over a span of the motion's clock, eased. A skirmish's start sets the map
// coming out of black at 86 % and settling to 100 % with the console rising after it
// (port_intro.cpp); a cutscene fades, zooms the picture and brings in cinema bars
// (port_cutscene.cpp); a map load goes out the same way and comes in as a skirmish does. The
// GPU presenter reads the map's scale and the console's (port_gpu.cpp, port_camera.cpp) and
// the overlay draws the black and the bars (port_overlay.cpp). Presentation only.
#include <windows.h>
#include <SDL3/SDL.h>
#include "port_present.h"  // PortPresentRedraw
#include "port_motion.h"
#include "port_overlay.h"  // PORT_OVERLAY_LAYER
#include "../front/port_ui.h"

namespace {

struct Move {
  float fFrom, fTo;
  double t0, fLength;  // from t0 for fLength seconds on the motion's clock
  int nEase;
};

const float kRest[MOTION_CHANNELS] = {1.0f, 0.0f, 1.0f, 0.0f, 0.0f};
Move g_moves[MOTION_CHANNELS] = {{1.0f, 1.0f, 0.0, 0.0, MOTION_EVEN}, {0.0f, 0.0f, 0.0, 0.0, MOTION_EVEN},
                                 {1.0f, 1.0f, 0.0, 0.0, MOTION_EVEN}, {0.0f, 0.0f, 0.0, 0.0, MOTION_EVEN},
                                 {0.0f, 0.0f, 0.0, 0.0, MOTION_EVEN}};
double g_fClock;
Uint64 g_nLastNs;
enum { ARRIVE_NONE, ARRIVE_NEXT, ARRIVE_HELD };
int g_nArrive;  // PortMotionArriveNext: the next game comes in as a skirmish does
bool g_bCover;  // PortMotionCover: black between games

float Eased(int nEase, float u)
{
  switch (nEase) {
    case MOTION_EASE: return UiEase(u);
    case MOTION_OUT: return 1.0f - (1.0f - u) * (1.0f - u) * (1.0f - u);
    case MOTION_IN_OUT: return u < 0.5f ? 4.0f * u * u * u : 1.0f - 4.0f * (1.0f - u) * (1.0f - u) * (1.0f - u);
    default: return u;
  }
}

float ValueAt(const Move &m, double t)
{
  if (t <= m.t0)
    return m.fFrom;
  if (m.fLength <= 0.0 || t >= m.t0 + m.fLength)
    return m.fTo;
  const float u = static_cast<float>((t - m.t0) / m.fLength);
  return m.fFrom + (m.fTo - m.fFrom) * Eased(m.nEase, u);
}

}  // namespace

double PortMotionClock(void)
{
  return g_fClock;
}

void PortMotionTick(void)
{
  static double s_fSlow = -1.0;
  if (s_fSlow < 0.0) {
    const char *psz = SDL_getenv("HYPERION_MOTION_SLOW");
    if (psz == NULL)
      psz = SDL_getenv("HYPERION_INTRO_SLOW");
    s_fSlow = psz != NULL && SDL_atof(psz) >= 1.0 ? SDL_atof(psz) : 1.0;
  }
  const Uint64 nNow = SDL_GetTicksNS();
  if (g_nLastNs != 0)
    g_fClock += SDL_min(static_cast<double>(nNow - g_nLastNs) / 1e9, 0.1) / s_fSlow;
  g_nLastNs = nNow;
  if (g_nArrive == ARRIVE_HELD) {
    g_nArrive = ARRIVE_NONE;
    PortMotionArrive();
  }
  if (PortMotionMoving())
    PortPresentRedraw();
}

void PortMotionArriveNext(void)
{
  g_nArrive = ARRIVE_NEXT;
}

void PortMotionArriveCancel(void)
{
  g_nArrive = ARRIVE_NONE;
  g_bCover = false;
}

void PortMotionCover(void)
{
  g_bCover = true;
}

bool PortMotionCovered(void)
{
  return g_bCover;
}

void PortMotionGameStarts(void)
{
  g_bCover = false;  // the game's own black takes over: the arrival's, or none
  if (g_nArrive == ARRIVE_NEXT) {
    PortMotionArriveHold();
    g_nArrive = ARRIVE_HELD;
  } else {
    PortMotionRest();
  }
}

float PortMotionValue(int nChannel)
{
  return nChannel >= 0 && nChannel < MOTION_CHANNELS ? ValueAt(g_moves[nChannel], g_fClock) : 0.0f;
}

void PortMotionRead(PortMotionNow *p)
{
  p->fMapScale = PortMotionValue(MOTION_MAP_SCALE);
  p->fHudShift = PortMotionValue(MOTION_HUD_SHIFT);
  p->fHudAlpha = PortMotionValue(MOTION_HUD_ALPHA);
  p->fBlack = PortMotionValue(MOTION_BLACK);
  p->fBars = PortMotionValue(MOTION_BARS);
}

void PortMotionTo(int nChannel, float fTo, double fSeconds, double fDelay, int nEase)
{
  if (nChannel < 0 || nChannel >= MOTION_CHANNELS)
    return;
  Move &m = g_moves[nChannel];
  m.fFrom = ValueAt(m, g_fClock + (fDelay > 0.0 ? fDelay : 0.0));
  m.fTo = fTo;
  m.t0 = g_fClock + (fDelay > 0.0 ? fDelay : 0.0);
  m.fLength = fSeconds > 0.0 ? fSeconds : 0.0;
  m.nEase = nEase;
  PortPresentRedraw();
}

void PortMotionSet(int nChannel, float fValue)
{
  PortMotionTo(nChannel, fValue, 0.0, 0.0, MOTION_EVEN);
  if (nChannel >= 0 && nChannel < MOTION_CHANNELS)
    g_moves[nChannel].fFrom = fValue;
}

void PortMotionRest(void)
{
  for (int i = 0; i < MOTION_CHANNELS; ++i)
    PortMotionSet(i, kRest[i]);
}

void PortMotionArriveHold(void)
{
  PortMotionSet(MOTION_MAP_SCALE, 0.86f);
  PortMotionSet(MOTION_HUD_SHIFT, 0.18f);
  PortMotionSet(MOTION_HUD_ALPHA, 0.0f);
  PortMotionSet(MOTION_BLACK, 1.0f);
}

// the skirmish's start as the front end's design times it: the map out of black (front.css
// .d-map), the view from 86 % to 100 % (front.js game, settle), the console rising 18 % as it
// shows (front.css d-hud-rise)
void PortMotionArrive(void)
{
  PortMotionTo(MOTION_BLACK, 0.0f, 1.2, 0.0, MOTION_EASE);
  PortMotionTo(MOTION_MAP_SCALE, 1.0f, PORT_MOTION_ARRIVE_SECONDS, 0.0, MOTION_OUT);
  PortMotionTo(MOTION_HUD_SHIFT, 0.0f, 0.6, 1.0, MOTION_EASE);
  PortMotionTo(MOTION_HUD_ALPHA, 1.0f, 0.6, 1.0, MOTION_EASE);
}

void PortMotionDepart(double fSeconds)
{
  PortMotionTo(MOTION_HUD_SHIFT, 0.18f, fSeconds * 0.5, 0.0, MOTION_EASE);
  PortMotionTo(MOTION_HUD_ALPHA, 0.0f, fSeconds * 0.5, 0.0, MOTION_EASE);
  PortMotionTo(MOTION_MAP_SCALE, 0.86f, fSeconds, 0.0, MOTION_IN_OUT);
  PortMotionTo(MOTION_BLACK, 1.0f, fSeconds, 0.0, MOTION_EASE);
}

bool PortMotionMoving(int nChannel)
{
  for (int i = 0; i < MOTION_CHANNELS; ++i)
    if ((nChannel < 0 || nChannel == i) && g_fClock < g_moves[i].t0 + g_moves[i].fLength &&
        g_moves[i].fFrom != g_moves[i].fTo)
      return true;
  return false;
}

// The black and the bars over the game's picture, in its box; true when it drew anything.
static bool LayerDraw(int nWidth, int nHeight, float fX, float fY, float fW, float fH)
{
  (void)nWidth;
  (void)nHeight;
  const float fBlack = PortMotionValue(MOTION_BLACK);
  const float fBars = PortMotionValue(MOTION_BARS);
  bool bDrew = false;
  if (fBars > 0.0f) {
    const float h = fH * SDL_min(fBars, 0.5f);
    UiFill(fX, fY, fW, h, UiRgb(0x000000));
    UiFill(fX, fY + fH - h, fW, h, UiRgb(0x000000));
    bDrew = true;
  }
  if (fBlack > 0.0f) {
    UiFill(fX, fY, fW, fH, UiRgb(0x000000, SDL_min(fBlack, 1.0f)));
    bDrew = true;
  }
  return bDrew;
}

bool PortMotionIsAction(unsigned int uMsg, long long lParam)
{
  if (uMsg >= WM_SYSKEYDOWN && uMsg <= WM_SYSCHAR)
    return (lParam & (1 << 29)) == 0;
  return (uMsg >= 0x100 && uMsg <= 0x102) || (uMsg >= 0x201 && uMsg <= 0x20E);
}

static bool LayerShown(void)
{
  return PortMotionValue(MOTION_BLACK) > 0.0f || PortMotionValue(MOTION_BARS) > 0.0f;
}

// The black and the bars as a layer over the game (port_overlay.h), the lowest.
const PortOverlayLayer kLayer = {
    "motion", PORT_OVERLAY_MOTION, 0,
    LayerShown, NULL, LayerDraw, NULL, NULL, NULL, NULL};
PORT_OVERLAY_LAYER(kLayer);
