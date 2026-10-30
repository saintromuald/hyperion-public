// port_camera.cpp -- the camera: how far the picture
// of the map is zoomed out from the game's own view. The game keeps its view as it is, so
// the simulation, the triggers and replays never see the zoom; the map's part of a pass is
// drawn over a larger render view around it (port_view.cpp, PortViewRenderBegin), and the
// presenter shows that at the camera's scale (port_gpu.cpp).
//
// A zoom of 2 shows twice the map across and down. It eases toward where the wheel or a
// key sends it, and holds at 1 when the GPU alone does not draw the map, or the presenter
// has no ground and HUD pipelines (PortGpuZoomCan); under SDL's renderer the software draws
// the render view instead (port_view.cpp, PortViewSoftShown). An HD frame zooms too, drawn through
// the camera (port_gpu_hd.cpp), without the ground layer.
#include <windows.h>
#include <math.h>
#include <stdlib.h>
#include "port_camera.h"
#include "port_view.h"  // PortViewZoomWhole
#include "../tools/port_control.h"
#include "../gameplay/port_cutscene.h"
#include "port_motion.h"
#include "../platform/port_settings.h"

enum { ZOOM_STEPS_A_DOUBLING = 4 };  // wheel notches from a zoom to twice it
static const float ZOOM_MOST = 32.0f;  // past any map's whole at the smallest view

static double g_fZoom = 1.0;        // the zoom drawn: the map's pixels to a screen pixel, across
static double g_fZoomTarget = 1.0;  // where it eases to
static LARGE_INTEGER g_last;        // when it last eased
static int g_nForce = -1;           // HYPERION_ZOOM_FORCE: the render view's path at zoom 1 too
static bool g_bStarted;
static bool g_bHold;                // PortCameraHold: easing to 1 for now
static bool g_bSlow;                // PortCameraSlow: TV's glide
static bool g_bEnvZoom;             // HYPERION_ZOOM gave the zoom
static bool g_bReplay;              // the game is a replay, whose zoom is kept apart
static float g_fSmoothLive = -1.0f; // the channel's camera.smooth for this run, -1 until set

// HYPERION_ZOOM sets the zoom a run starts at, HYPERION_ZOOM_FORCE=1 takes the render
// view's path at any zoom, 1 included.
static void CameraStart(void)
{
  if (g_bStarted)
    return;
  g_bStarted = true;
  char sz[32];
  if (GetEnvironmentVariableA("HYPERION_ZOOM", sz, sizeof sz) > 0) {
    const double f = atof(sz);
    if (f >= 0.5 && f <= ZOOM_MOST) {
      g_fZoom = g_fZoomTarget = f;
      g_bEnvZoom = true;
    }
  }
  g_nForce = GetEnvironmentVariableA("HYPERION_ZOOM_FORCE", sz, sizeof sz) > 0 && sz[0] == '1' ? 1 : 0;
  PortControlValue(
      "zoom", [] { return static_cast<float>(g_fZoomTarget); }, [](float f) { PortCameraZoomTo(f); }, 0.5f, ZOOM_MOST,
      1.0f, "how far the map is zoomed out: 1 the game's own view, 2 twice the map across and down", 0.0f);
  // the smoothCamera setting for this run, not saved: Debug Tools' demo reel shows 1.17's steps
  // beside the glide with it
  PortControlValue(
      "camera.smooth", [] { return static_cast<float>(PortCameraSmooth()); },
      [](float f) { g_fSmoothLive = f >= 0.5f ? 1.0f : 0.0f; }, 0.0f, 1.0f, 1.0f,
      "the camera glides between game frames (1), or moves with the game's view in its steps as 1.17's does (0); "
      "for this run, the smoothCamera setting left as it is",
      1.0f);
}

// In a game the zoomMax setting, 2 unless set; in a replay and while a cutscene plays the whole
// map, or the setting if that is more.
double PortCameraZoomMax(void)
{
  double f = 2.0;
  PortSettingNumber("hyperion", "zoomMax", &f);
  f = f < 1.0 ? 1.0 : f > 8.0 ? 8.0 : f;
  const double fWhole = PortViewZoomWhole();
  f = fWhole > f ? (fWhole < ZOOM_MOST ? fWhole : ZOOM_MOST) : f;
  // under SDL's renderer, no further than the software draws each pass
  const double fSoft = PortViewZoomSoftMost();
  return fSoft > 0.0 && f > fSoft ? fSoft : f;
}

void PortCameraGameStarts(int bReplay)
{
  CameraStart();
  g_bReplay = bReplay != 0;
  if (g_bEnvZoom)
    return;
  double f = 1.0;
  PortSettingNumber("hyperion", g_bReplay ? "replayZoom" : "zoom", &f);
  g_fZoom = g_fZoomTarget = f > 0.0 ? f : 1.0;  // the limits clamp it (PortCameraStep)
}

void PortCameraRemember(void)
{
  PortSettingSetNumber("hyperion", g_bReplay ? "replayZoom" : "zoom", g_fZoomTarget);
}

int PortCameraSmooth(void)
{
  if (g_fSmoothLive >= 0.0f)
    return g_fSmoothLive != 0.0f ? 1 : 0;
  double f = 1.0;
  PortSettingNumber("hyperion", "smoothCamera", &f);
  return f != 0.0 ? 1 : 0;
}

// a replay may look closer, and a cutscene in a game
double PortCameraZoomMin(void)
{
  return PortViewZoomWhole() > 0.0 || PortCutsceneRunning() ? 0.5 : 1.0;
}

void PortCameraZoomTo(double fZoom)
{
  CameraStart();
  const double fMax = PortCameraZoomMax();
  const double fMin = PortCameraZoomMin();
  g_fZoomTarget = fZoom < fMin ? fMin : fZoom > fMax ? fMax : fZoom;
}

void PortCameraZoomSet(double fZoom)
{
  CameraStart();
  const double fMax = PortCameraZoomMax();
  const double fMin = PortCameraZoomMin();
  g_fZoom = g_fZoomTarget = fZoom < fMin ? fMin : fZoom > fMax ? fMax : fZoom;
}

void PortCameraSlow(int bSlow)
{
  g_bSlow = bSlow != 0;
}

int PortCameraIsSlow(void)
{
  return g_bSlow ? 1 : 0;
}

void PortCameraWheel(double fNotches)
{
  CameraStart();
  PortCameraZoomTo(g_fZoomTarget * pow(2.0, fNotches / ZOOM_STEPS_A_DOUBLING));
  PortCameraRemember();
}

double PortCameraZoomTarget(void)
{
  CameraStart();
  return g_bHold ? 1.0 : g_fZoomTarget;
}

double PortCameraZoom(void)
{
  CameraStart();
  return g_fZoom;
}

int PortCameraForced(void)
{
  CameraStart();
  return g_nForce;
}

double PortCameraShown(void)
{
  PortCameraStep();
  return g_fZoom / PortMotionValue(MOTION_MAP_SCALE);
}

int PortCameraScrollPixels(int nPixels)
{
  CameraStart();
  if (g_fZoom == 1.0)
    return nPixels;
  return static_cast<int>(floor(nPixels * g_fZoom + 0.5));
}

void PortCameraHold(int bHold)
{
  g_bHold = bHold != 0;
}

int PortCameraStep(void)
{
  CameraStart();
  LARGE_INTEGER now, freq;
  QueryPerformanceCounter(&now);
  QueryPerformanceFrequency(&freq);
  double dt = g_last.QuadPart != 0 ? static_cast<double>(now.QuadPart - g_last.QuadPart) / freq.QuadPart : 0.0;
  g_last = now;
  if (dt > 0.1)
    dt = 0.1;
  // a replay's whole map is past a game's limit: the zoom kept from one comes back at once
  const double fMax = PortCameraZoomMax();
  if (g_fZoomTarget > fMax)
    g_fZoomTarget = fMax;
  if (g_fZoom > fMax)
    g_fZoom = fMax;
  // a replay's closer look is not a game's
  const double fMin = PortCameraZoomMin();
  if (g_fZoomTarget < fMin)
    g_fZoomTarget = fMin;
  if (g_fZoom < fMin)
    g_fZoom = fMin;
  const double fTarget = g_bHold ? 1.0 : g_fZoomTarget;
  if (g_fZoom != fTarget) {
    // most of the way in about a fifth of a second, in equal ratios, so a doubling feels as
    // fast going out as coming back
    const double k = !PortCameraSmooth() ? 1.0 : 1.0 - exp(-dt / (g_bSlow ? 0.5 : 0.06));
    g_fZoom = exp(log(g_fZoom) + (log(fTarget) - log(g_fZoom)) * k);
    if (fabs(g_fZoom - fTarget) < 0.002)
      g_fZoom = fTarget;
  }
  return g_fZoom != fTarget ? 1 : 0;
}
