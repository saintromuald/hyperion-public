// port_overlay.cpp -- what Hyperion draws over a game at the window's own resolution, in
// its own fonts: Settings' game timer, actions per minute and frame rate, which
// StarCraft: Remastered shows and 1.17 does not (docs/front-end.md), and the layers other
// modules add (port_overlay.h): Debug Tools', the benchmark's, the trainer's and the others,
// each drawn and asked for the mouse and keys through one list. The GPU presenter draws the
// list after the game's frame (port_gpu_ui.cpp, GpuUiOverlay); SDL's renderer shows none.
// Presentation only: the actions are counted from the commands the player sends, which are
// left as they were.
#include <windows.h>
#include <stdio.h>
#include <SDL3/SDL.h>
#include "port_overlay.h"
#include "port_motion.h"  // PortMotionCovered
#include "../loop/port_clock.h"  // PortClockFrameMs
#include "../platform/port_settings.h"
#include "../front/port_ui.h"
#include "../hud/port_hud.h"  // PortHudCountersOff, PortHudCovers
#include "../../h/net_cmds.h"  // NET_CMD_SELECT_CHUNK
#include "../../gen/globals117.h"
#include "../platform/port_globals117.h"

namespace {

// A game minute at Fastest, 42 ms a frame, in frames, as Remastered's timer and its
// actions per minute count time.
const unsigned kMinuteFrames = 1429;
const unsigned kActionsKept = 4096;

bool g_bRead;
bool g_bTimer = true;
bool g_bApm;
bool g_bFps;

// the frames of the player's actions in the last game minute, oldest first, in a ring
unsigned g_anAction[kActionsKept];
unsigned g_nActionFirst;
unsigned g_nActions;
unsigned g_nGameFrom = ~0u;  // the frame the game's count started at

// frames shown, counted a second at a time
Uint64 g_nFpsSince;
int g_nFpsFrames;
int g_nFps = -1;

// The layers, by nOrder, the lowest first. Filled as the modules' statics are made, before
// main, so a plain array: it is zero before any of them.
const int kLayersMax = 32;
const PortOverlayLayer *g_apLayer[kLayersMax];
int g_nLayers;

bool Shown(const PortOverlayLayer *pLayer)
{
  return pLayer->Shown != NULL && pLayer->Shown();
}

// Every layer's Draw, the lowest first; whether any drew.
bool DrawLayers(int nWidth, int nHeight, float fX, float fY, float fW, float fH)
{
  bool bDrew = false;
  for (int i = 0; i < g_nLayers; ++i)
    if (g_apLayer[i]->Draw != NULL)
      bDrew |= g_apLayer[i]->Draw(nWidth, nHeight, fX, fY, fW, fH);
  return bDrew;
}

bool Flag(const char *pszName, bool bDefault)
{
  double d = bDefault ? 1.0 : 0.0;
  PortSettingNumber("hyperion", pszName, &d);
  return d != 0.0;
}

void ReadSettings(void)
{
  if (g_bRead)
    return;
  g_bRead = true;
  g_bTimer = Flag("gameTimer", true);
  g_bApm = Flag("apm", false);
  g_bFps = Flag("fps", false);
}

// The commands that are not a player's own act: the turn's keep-alive, saving, loading,
// restarting, pausing and resuming, the sync check, chat, and the lobby's.
bool IsAction(BYTE bCmd)
{
  switch (bCmd) {
    case 0x05:  // keep alive
    case 0x06:  // save game
    case 0x07:  // load game
    case 0x08:  // restart
    case 0x10:  // pause
    case 0x11:  // resume
    case 0x37:  // sync check
    case 0x5C:  // chat
      return false;
    default:
      return bCmd < 0x3C || bCmd > 0x4F;  // the lobby's commands
  }
}

// The game time, m:ss or h:mm:ss, at the game's speed (PortClockFrameMs).
void TimeText(unsigned nFrames, char *psz, size_t cch)
{
  const unsigned nSeconds = static_cast<unsigned>(static_cast<unsigned long long>(nFrames) * PortClockFrameMs() / 1000);
  if (nSeconds >= 3600)
    snprintf(psz, cch, "%u:%02u:%02u", nSeconds / 3600, nSeconds / 60 % 60, nSeconds % 60);
  else
    snprintf(psz, cch, "%u:%02u", nSeconds / 60, nSeconds % 60);
}

// The actions of the last game minute, scaled to a minute while the game is younger; -1
// for the first ten seconds, when a rate means little.
int Apm(unsigned nNow)
{
  if (g_nGameFrom == ~0u || nNow < g_nGameFrom)
    return -1;
  while (g_nActions > 0 && nNow - g_anAction[g_nActionFirst] >= kMinuteFrames) {
    g_nActionFirst = (g_nActionFirst + 1) % kActionsKept;
    --g_nActions;
  }
  const unsigned nSpan = SDL_min(nNow - g_nGameFrom, kMinuteFrames);
  if (nSpan < 240)
    return -1;
  return static_cast<int>(g_nActions * kMinuteFrames / nSpan);
}

}  // namespace

void PortOverlayReread(void)
{
  g_bRead = false;
}

// From netmgr_queue_cmd: a command the player sends.
void PortOverlayAction(const void *pCmd)
{
  if (gwGameMode != 3 || gPlayback != 0 || !IsAction(*static_cast<const BYTE *>(pCmd)))
    return;
  // a selection's chunks after its first (net_cmds.h, 0x8C) are the same act
  if (*static_cast<const BYTE *>(pCmd) == NET_CMD_SELECT_CHUNK &&
      (static_cast<const BYTE *>(pCmd)[2] & SELECT_CHUNK_NEXT) != 0)
    return;
  const unsigned nNow = oc_dwReplayFrame;
  if (g_nGameFrom == ~0u || nNow < g_nGameFrom) {
    // a new game: its frames start again
    g_nGameFrom = nNow;
    g_nActions = 0;
  }
  if (g_nActions == kActionsKept) {
    g_nActionFirst = (g_nActionFirst + 1) % kActionsKept;
    --g_nActions;
  }
  g_anAction[(g_nActionFirst + g_nActions) % kActionsKept] = nNow;
  ++g_nActions;
}

// From the presenter, for each frame it shows.
void PortOverlayFrameShown(void)
{
  const Uint64 nNow = SDL_GetTicks();
  if (g_nFpsSince == 0)
    g_nFpsSince = nNow;
  ++g_nFpsFrames;
  if (nNow - g_nFpsSince >= 1000) {
    g_nFps = static_cast<int>(g_nFpsFrames * 1000 / (nNow - g_nFpsSince));
    g_nFpsFrames = 0;
    g_nFpsSince = nNow;
  }
}

bool PortOverlayAdd(const PortOverlayLayer *pLayer)
{
  if (g_nLayers == kLayersMax)
    return false;
  int i = g_nLayers++;
  for (; i > 0 && g_apLayer[i - 1]->nOrder > pLayer->nOrder; --i)
    g_apLayer[i] = g_apLayer[i - 1];
  g_apLayer[i] = pLayer;
  return true;
}

bool PortOverlayConsoleAt(float fX, float fY)
{
  for (int i = 0; i < g_nLayers; ++i)
    if ((g_apLayer[i]->uFlags & PORT_OVERLAY_CONSOLE) != 0 && g_apLayer[i]->Outside != NULL &&
        g_apLayer[i]->Outside(fX, fY))
      return true;
  return false;
}

bool PortOverlayHas(unsigned uFlag)
{
  for (int i = 0; i < g_nLayers; ++i)
    if ((g_apLayer[i]->uFlags & uFlag) != 0 && Shown(g_apLayer[i]))
      return true;
  return false;
}

void PortOverlayGameArea(float fW, float fH, float *pfX, float *pfY, float *pfW, float *pfH)
{
  for (int i = g_nLayers - 1; i >= 0; --i)
    if (g_apLayer[i]->Area != NULL && Shown(g_apLayer[i])) {
      g_apLayer[i]->Area(fW, fH, pfX, pfY, pfW, pfH);
      return;
    }
  *pfX = *pfY = 0.0f;
  *pfW = fW;
  *pfH = fH;
}

bool PortOverlayHold(unsigned uMsg, unsigned long long wParam, long long lParam)
{
  for (int i = g_nLayers - 1; i >= 0; --i)
    if (g_apLayer[i]->Hold != NULL && g_apLayer[i]->Hold(uMsg, wParam, lParam))
      return true;
  return false;
}

bool PortOverlayMouse(unsigned uMsg, unsigned long long wParam, long long lParam)
{
  for (int i = g_nLayers - 1; i >= 0; --i)
    if (g_apLayer[i]->Mouse != NULL && g_apLayer[i]->Mouse(uMsg, wParam, lParam))
      return true;
  return false;
}

bool PortOverlayWheel(int nDelta)
{
  for (int i = g_nLayers - 1; i >= 0; --i)
    if (g_apLayer[i]->Wheel != NULL && Shown(g_apLayer[i]) && g_apLayer[i]->Wheel(nDelta))
      return true;
  return false;
}

// A key down or the character it made, in a game; a held key's repeats come with bRepeat.
bool PortOverlayKey(unsigned uMsg, unsigned long long wParam, long long lParam)
{
  const bool bSys = uMsg == WM_SYSKEYDOWN || uMsg == WM_SYSCHAR;
  if (gwGameMode != 3 || (uMsg != WM_KEYDOWN && uMsg != WM_CHAR && !bSys))
    return false;
  const bool bDown = uMsg == WM_KEYDOWN || uMsg == WM_SYSKEYDOWN;
  const bool bRepeat = bDown && (lParam & 0x40000000) != 0;
  for (int i = g_nLayers - 1; i >= 0; --i)
    if (g_apLayer[i]->Key != NULL && (!bSys || (g_apLayer[i]->uFlags & PORT_OVERLAY_SYSKEYS) != 0) &&
        !(uMsg == WM_KEYDOWN && (g_apLayer[i]->uFlags & PORT_OVERLAY_BEFORE_ACCEL) != 0) &&
        g_apLayer[i]->Key(static_cast<unsigned>(wParam), bDown, bRepeat))
      return true;
  return false;
}

bool PortOverlayKeyFirst(unsigned uMsg, unsigned long long wParam, long long lParam)
{
  if (gwGameMode != 3 || uMsg != WM_KEYDOWN)
    return false;
  const bool bRepeat = (lParam & 0x40000000) != 0;
  for (int i = g_nLayers - 1; i >= 0; --i)
    if (g_apLayer[i]->Key != NULL && (g_apLayer[i]->uFlags & PORT_OVERLAY_BEFORE_ACCEL) != 0 &&
        g_apLayer[i]->Key(static_cast<unsigned>(wParam), true, bRepeat))
      return true;
  return false;
}

bool PortOverlayOutside(float fX, float fY)
{
  for (int i = 0; i < g_nLayers; ++i)
    if ((g_apLayer[i]->uFlags & PORT_OVERLAY_CONSOLE) == 0 && g_apLayer[i]->Outside != NULL &&
        g_apLayer[i]->Outside(fX, fY))
      return true;
  return false;
}

// The overlay's list for a target nWidth x nHeight with the game's frame in the box
// (fX, fY, fW, fH): false when there is nothing to draw.
bool PortOverlayBuild(int nWidth, int nHeight, float fX, float fY, float fW, float fH)
{
  ReadSettings();
  bool abShown[kLayersMax];
  bool bAny = false;
  for (int i = 0; i < g_nLayers; ++i)
    bAny |= abShown[i] = Shown(g_apLayer[i]);
  // between the games of a map switch, the whole window black, or with a layer up that stays
  // over it (Debug Tools'), the game's box, and that layer
  if (gwGameMode != 3 && PortMotionCovered()) {
    UiBegin(nWidth, nHeight);
    bool bOver = false;
    for (int i = 0; i < g_nLayers; ++i)
      bOver |= abShown[i] && (g_apLayer[i]->uFlags & PORT_OVERLAY_OVER_COVER) != 0;
    if (!bOver) {
      UiFill(0.0f, 0.0f, static_cast<float>(nWidth), static_cast<float>(nHeight), UiRgb(0x000000));
      return true;
    }
    UiFill(fX, fY, fW, fH, UiRgb(0x000000));  // first: in its full mode the panels are over the box
    for (int i = 0; i < g_nLayers; ++i)
      if (abShown[i] && (g_apLayer[i]->uFlags & PORT_OVERLAY_OVER_COVER) != 0 && g_apLayer[i]->Draw != NULL)
        g_apLayer[i]->Draw(nWidth, nHeight, fX, fY, fW, fH);
    return true;
  }
  if (gwGameMode != 3) {
    bool bOut = false;
    for (int i = 0; i < g_nLayers; ++i)
      bOut |= abShown[i] && (g_apLayer[i]->uFlags & PORT_OVERLAY_OUT_OF_GAME) != 0 && g_apLayer[i]->Draw != NULL;
    if (!bOut)
      return false;
    UiBegin(nWidth, nHeight);
    bool bDrew = false;
    for (int i = 0; i < g_nLayers; ++i)
      if (abShown[i] && (g_apLayer[i]->uFlags & PORT_OVERLAY_OUT_OF_GAME) != 0 && g_apLayer[i]->Draw != NULL)
        bDrew |= g_apLayer[i]->Draw(nWidth, nHeight, fX, fY, fW, fH);
    return bDrew;
  }
  if (!g_bTimer && !g_bApm && !g_bFps && !bAny)
    return false;
  bool bOwnCounters = false;
  for (int i = 0; i < g_nLayers; ++i)
    bOwnCounters |= abShown[i] && (g_apLayer[i]->uFlags & PORT_OVERLAY_OWN_COUNTERS) != 0;
  const unsigned nNow = oc_dwReplayFrame;
  if (g_nGameFrom == ~0u || nNow < g_nGameFrom) {
    g_nGameFrom = nNow;
    g_nActions = 0;
  }
  char aszLines[3][32];
  const char *apszLabels[3];
  int nLines = 0;
  // a replay layout with no clock of its own has its position readout for one
  const bool bHudOff = PortHudCountersOff();
  if (g_bTimer && !bOwnCounters && !bHudOff) {  // a benchmark run's overlay has the game's time
    TimeText(nNow, aszLines[nLines], sizeof aszLines[0]);
    apszLabels[nLines++] = NULL;
  }
  if (g_bApm && gPlayback == 0 && !bHudOff) {
    const int nApm = Apm(nNow);
    if (nApm >= 0)
      snprintf(aszLines[nLines], sizeof aszLines[0], "%d", nApm);
    else
      snprintf(aszLines[nLines], sizeof aszLines[0], "-");
    apszLabels[nLines++] = "APM";
  }
  if (g_bFps && !bOwnCounters) {
    if (g_nFps >= 0)
      snprintf(aszLines[nLines], sizeof aszLines[0], "%d", g_nFps);
    else
      snprintf(aszLines[nLines], sizeof aszLines[0], "-");
    apszLabels[nLines++] = "FPS";
  }
  // the layers over the counters, Debug Tools' over the picture's black and a scene's words: its
  // full mode's panels are over the game's box, and a map switch's fade is not to take them
  UiBegin(nWidth, nHeight);
  if (nLines == 0)
    return DrawLayers(nWidth, nHeight, fX, fY, fW, fH);

  // sized by the game's frame, 480 of its rows to the box's height: at the top left,
  // where 1.17 draws nothing, the timer larger than the counters under it
  const float k = fH / 480.0f;
  const float fPad = 4.0f * k;
  const float fBig = 11.0f * k;
  const float fSmall = 8.0f * k;
  const float fLabel = 6.0f * k;
  const UiColor ink = UiRgb(0xffffff);
  const UiColor dim = UiRgb(0x9eccf3);
  const UiColor shade = UiRgb(0x000000, 0.55f);
  float fWide = 0.0f;
  float fTall = fPad;
  for (int i = 0; i < nLines; ++i) {
    const float fSize = apszLabels[i] == NULL ? fBig : fSmall;
    float w = UiTextWidth(UI_FACE_BOLD, fSize, 0.0f, aszLines[i]);
    if (apszLabels[i] != NULL)
      w += UiTextWidth(UI_FACE_WIDE, fLabel, 0.5f * k, apszLabels[i]) + 3.0f * k;
    fWide = SDL_max(fWide, w);
    fTall += fSize * 1.15f;
  }
  const float x0 = fX + 6.0f * k;
  const float y0 = fY + 6.0f * k;
  // none under a layer's panels: a window drawn on the HUD's plates (a music add-on's) leaves
  // its face to them, which are under all of the overlay; nor under the HUD's own panels,
  // whose screen is the game's box, 1080 units high
  const float fUnit = fH / 1080.0f;
  for (int n = 0; n < 25; ++n) {
    const float px = x0 + (fWide + 2.0f * fPad) * (n % 5) / 4.0f, py = y0 + (fTall + fPad) * (n / 5) / 4.0f;
    bool bUnder = fUnit > 0.0f && PortHudCovers((px - fX) / fUnit, (py - fY) / fUnit);
    for (int i = 0; i < g_nLayers && !bUnder; ++i)
      bUnder = abShown[i] && g_apLayer[i]->Outside != NULL && g_apLayer[i]->Outside(px, py);
    if (bUnder)
      return DrawLayers(nWidth, nHeight, fX, fY, fW, fH);
  }
  UiFill(x0, y0, fWide + 2.0f * fPad, fTall + fPad, shade, 3.0f * k);
  float y = y0 + fPad;
  for (int i = 0; i < nLines; ++i) {
    const float fSize = apszLabels[i] == NULL ? fBig : fSmall;
    float fAscent, fDescent, fGap;
    UiFontMetrics(UI_FACE_BOLD, fSize, &fAscent, &fDescent, &fGap);
    const float fBase = y + fAscent * 0.95f;
    float x = x0 + fPad;
    if (apszLabels[i] != NULL) {
      x += UiText(x, fBase, UI_FACE_WIDE, fLabel, 0.5f * k, dim, apszLabels[i]) + 3.0f * k;
    }
    UiText(x, fBase, UI_FACE_BOLD, fSize, 0.0f, ink, aszLines[i]);
    y += fSize * 1.15f;
  }
  DrawLayers(nWidth, nHeight, fX, fY, fW, fH);
  return true;
}
