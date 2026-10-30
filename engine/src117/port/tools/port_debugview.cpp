// port_debugview.cpp -- Debug Tools' overlay: the game in a window inside the screen
// with the readings round it, as the benchmark lays its run out, so nothing is drawn over
// the game; or the game filling the screen with the same panels see-through over it; or a small
// counter (docs/modules/tools.md, "Debug Tools and the canvas"). F6 (port_keys.cpp) steps
// through them while the live channel is open, as do the channel's "overlay" and the panel's
// buttons; while it is off a small Overlay button opens it in the canvas or a developer's run
// (--control, HYPERION_DEBUG_VIEW), not in a campaign's mission or a skirmish that a tool mod's
// channel happens to be open over, and the counter is a button too.
//
// The top strip's buttons change Settings' rows as Accept would, and restart the clock. The
// readings are port_clock.cpp's on the left and port_present.cpp's and port_perf.cpp's on the
// right. It is a layer over the game (port_overlay.h): the GPU presenter places the game in its
// area and draws it after the game, and the mouse over the panels reaches it, not the game.
// Presentation only.
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <string>
#include <SDL3/SDL.h>
#include "port_bench.h"  // PortBenchSecond
#include "port_benchview.h"  // PortBenchViewShown
#include "../loop/port_clock.h"
#include "port_debugview.h"
#include "../gameplay/port_keys.h"  // PortHotkey, PORT_HOTKEY_DEBUG_VIEW, PortHotkeyName
#include "../loop/port_perf.h"  // PERF_PARTS, PortPerfPartName
#include "../render/port_present.h"  // PortCursorFree, PortPerfLiveSeconds, PortPresentRedraw
#include "port_control.h"
#include "../render/port_motion.h"  // PortMotionCovered: between a map switch's two games
#include "../render/port_overlay.h"  // PORT_OVERLAY_LAYER
#include "../front/port_ui.h"
#include "../front/port_front.h"  // PortSettingLiveWord, PortSettingLiveSet
#include "../gameplay/port_launch.h"  // PortLaunchIsCanvas
#include "../../h/hxrules.h"  // RulesCampaignMission
#include "../platform/port_paths.h"  // PortCommandOptions
#include "../../gen/globals117.h"
#include "../platform/port_globals117.h"

namespace {

enum { VIEW_OFF, VIEW_WINDOW, VIEW_FULL, VIEW_COUNTER, VIEW_MODES };
const char *const kModeNames[VIEW_MODES] = {"off", "window", "full", "counter"};

int g_nMode;

// The stage, in units: 1080 high and at least 1920 wide, as port_ui.cpp's.
const float kMargin = 16.0f;
const float kTop = 52.0f;
const float kColumn = 340.0f;
const float kFoot = 190.0f;

float StageScale(float fW, float fH)
{
  return fW / fH >= 1920.0f / 1080.0f ? fH / 1080.0f : fW / 1920.0f;
}

// What the game's window and the panels take, in target pixels.
struct Layout {
  float k;                  // pixels a unit
  float fUnitsW, fUnitsH;
  float areaX, areaY, areaW, areaH;
};

Layout LayoutFor(float fW, float fH)
{
  Layout l;
  l.k = StageScale(fW, fH);
  l.fUnitsW = fW / l.k;
  l.fUnitsH = fH / l.k;
  if (g_nMode == VIEW_WINDOW) {
    const float x = kMargin * 2.0f + kColumn;
    const float y = kTop + kMargin;
    l.areaX = x * l.k;
    l.areaY = y * l.k;
    l.areaW = (l.fUnitsW - 2.0f * x) * l.k;
    l.areaH = (l.fUnitsH - y - kFoot - 2.0f * kMargin) * l.k;
  } else {
    l.areaX = l.areaY = 0.0f;
    l.areaW = fW;
    l.areaH = fH;
  }
  return l;
}

// The colours, as Debug Tools' panel has them in its dark theme.
const unsigned kPanel = 0x0d141c, kLine = 0x2a3a4a, kInk = 0xe6e8eb, kDim = 0x9aa2ae, kAccent = 0xe8b84a,
               kGood = 0x6cc48a, kBad = 0xf08a80, kFps = 0x7bd88f, kGfps = 0x66d5fe;
const unsigned kParts[PERF_PARTS] = {0x8fb8de, 0xb0705a, 0xe6b450, 0x6f8fbf, 0x4fc3d9, 0x7bd88f,
                                     0xc792ea, 0x9aa6b2, 0x5fb3a1, 0xd6d26b, 0xf07178};
const char *const kSpeeds[7] = {"Slowest", "Slower", "Slow", "Normal", "Fast", "Faster", "Fastest"};

// The buttons of the last frame drawn, in target pixels.
enum Action {
  ACT_NONE, ACT_CLOCK, ACT_FRAME_RATE, ACT_VSYNC, ACT_SPEED, ACT_MODE, ACT_CLOSE, ACT_OPEN, ACT_COPY_LINK, ACT_COPY_AI
};
struct Button {
  float x, y, w, h;
  Action act;
};
Button g_buttons[12];
int g_nButtons;
int g_nHot = -1;
float g_gameX, g_gameY, g_gameW, g_gameH;  // the game's box, last drawn
bool g_bDrawn;
bool g_bOpener;                            // the Overlay button was drawn, the overlay being off
Action g_nCopied = ACT_NONE;               // the copy button last pressed, which says Copied
Uint64 g_nCopiedAt;                        // when, for two seconds

// The overlay's key as the buttons name it, " (F6)", or "" when it has none.
std::string KeyNote()
{
  const unsigned nKey = PortHotkey(PORT_HOTKEY_DEBUG_VIEW);
  if (nKey == 0)
    return "";
  char sz[48];
  PortHotkeyName(nKey, sz, sizeof sz);
  return std::string(" (") + sz + ")";
}

// Drawing, in units scaled by k from an origin.
struct Pen {
  float k;
  float fAlpha;  // the panels' backgrounds: solid round the window, see-through over the game
};

void Panel(const Pen &p, float x, float y, float w, float h)
{
  UiFill(x * p.k, y * p.k, w * p.k, h * p.k, UiRgb(kPanel, p.fAlpha), 6.0f * p.k);
  UiRing(x * p.k, y * p.k, w * p.k, h * p.k, UiRgb(kLine, 0.9f), 6.0f * p.k, 1.0f);
}

float Text(const Pen &p, float x, float y, float fSize, unsigned nRgb, const char *psz, UiFace face = UI_FACE_TEXT)
{
  return UiText(x * p.k, y * p.k, face, fSize * p.k, face == UI_FACE_WIDE ? 0.08f * fSize * p.k : 0.0f, UiRgb(nRgb),
                psz) / p.k;
}

float TextWidth(const Pen &p, float fSize, const char *psz, UiFace face = UI_FACE_TEXT)
{
  return UiTextWidth(face, fSize * p.k, face == UI_FACE_WIDE ? 0.08f * fSize * p.k : 0.0f, psz) / p.k;
}

void Heading(const Pen &p, float x, float y, const char *psz)
{
  Text(p, x, y, 11.0f, kDim, psz, UI_FACE_WIDE);
}

// A figure: its label small, its value large, in a box w units wide.
void Figure(const Pen &p, float x, float y, float w, const char *pszLabel, const char *pszValue, unsigned nRgb = kInk)
{
  UiRing(x * p.k, y * p.k, w * p.k, 52.0f * p.k, UiRgb(kLine), 4.0f * p.k, 1.0f);
  Text(p, x + 8.0f, y + 17.0f, 12.0f, kDim, pszLabel);
  Text(p, x + 8.0f, y + 43.0f, 21.0f, nRgb, pszValue, UI_FACE_BOLD);
}

// fMinW: at least this wide, so a button whose label changes keeps its size
void AddButton(const Pen &p, float x, float y, const char *psz, Action act, bool bLit = false, float fMinW = 0.0f)
{
  const float w = SDL_max(TextWidth(p, 13.0f, psz) + 20.0f, fMinW), h = 30.0f;
  const int i = g_nButtons;
  const bool bHot = g_nHot == i;
  UiFill(x * p.k, y * p.k, w * p.k, h * p.k, UiRgb(bLit ? kAccent : bHot ? kLine : kPanel, bLit ? 1.0f : 0.95f),
         4.0f * p.k);
  UiRing(x * p.k, y * p.k, w * p.k, h * p.k, UiRgb(bLit ? kAccent : kLine), 4.0f * p.k, 1.0f);
  Text(p, x + 10.0f, y + 20.0f, 13.0f, bLit ? kPanel : kInk, psz);
  if (g_nButtons < static_cast<int>(sizeof g_buttons / sizeof g_buttons[0]))
    g_buttons[g_nButtons++] = {x * p.k, y * p.k, w * p.k, h * p.k, act};
}

float ButtonWidth(const Pen &p, const char *psz)
{
  return TextWidth(p, 13.0f, psz) + 20.0f;
}

// A plot of values, oldest left, from 0 to fTop, with a dashed line at fMark when above 0.
void Plot(const Pen &p, float x, float y, float w, float h, const float *pf, int n, float fTop, unsigned nRgb,
          float fMark, unsigned nMarkRgb)
{
  UiRing(x * p.k, y * p.k, w * p.k, h * p.k, UiRgb(kLine), 3.0f * p.k, 1.0f);
  if (fTop <= 0.0f)
    return;
  auto Y = [&](float v) { return (y + h - 3.0f - (v > fTop ? fTop : v) / fTop * (h - 6.0f)) * p.k; };
  if (fMark > 0.0f)
    for (float d = 0.0f; d < w - 4.0f; d += 8.0f)
      UiLine((x + 2.0f + d) * p.k, Y(fMark), (x + 2.0f + d + 4.0f) * p.k, Y(fMark), UiRgb(nMarkRgb, 0.9f), 1.0f);
  for (int i = 1; i < n; ++i)
    UiLine((x + 2.0f + (i - 1) * (w - 4.0f) / (n - 1)) * p.k, Y(pf[i - 1]), (x + 2.0f + i * (w - 4.0f) / (n - 1)) * p.k,
           Y(pf[i]), UiRgb(nRgb), 1.5f);
}

std::string Word(const char *pszSetting)
{
  float f;
  return PortSettingLiveWord(pszSetting, &f);
}

void TopStrip(const Pen &p, float fUnitsW, const PortClockNow &c)
{
  Panel(p, kMargin, 8.0f, fUnitsW - 2.0f * kMargin, kTop - 12.0f);
  float x = kMargin + 14.0f;
  x += Text(p, x, 34.0f, 14.0f, kAccent, "DEBUG TOOLS", UI_FACE_WIDE) + 18.0f;
  char sz[160];
  const unsigned nSeconds = static_cast<unsigned>(static_cast<unsigned long long>(oc_dwReplayFrame) * PortClockFrameMs() / 1000);
  const DWORD dwSpeed = gOptions <= 6 ? gOptions : 6;
  snprintf(sz, sizeof sz, "%s  \xc2\xb7  frame %u  \xc2\xb7  %u:%02u game time  \xc2\xb7  %s  \xc2\xb7  clock %.0f s",
           gPlayback ? "a replay" : "a game", oc_dwReplayFrame, nSeconds / 60, nSeconds % 60, kSpeeds[dwSpeed],
           c.fSeconds);
  x += Text(p, x, 33.0f, 14.0f, kInk, sz) + 18.0f;
  // the mouse: how to free it from the game's picture to reach the panels, or how to take it back
  const bool bFree = PortCursorFree();
  const char *pszMouse = bFree ? "Mouse free: click the game to take it back" : "Ctrl+Alt frees the mouse for these panels";
  const float fChipW = TextWidth(p, 13.0f, pszMouse, UI_FACE_BOLD) + 22.0f;
  UiFill(x * p.k, 13.0f * p.k, fChipW * p.k, 30.0f * p.k, UiRgb(bFree ? kAccent : kPanel, 1.0f), 15.0f * p.k);
  UiRing(x * p.k, 13.0f * p.k, fChipW * p.k, 30.0f * p.k, UiRgb(kAccent), 15.0f * p.k, 1.0f);
  Text(p, x + 11.0f, 33.0f, 13.0f, bFree ? kPanel : kAccent, pszMouse, UI_FACE_BOLD);
  // the buttons, from the right
  float fFrame = 0.0f, fSync = 0.0f, fSpeed = 0.0f;
  const std::string frame = "Frame rate: " + PortSettingLiveWord("Frame rate", &fFrame);
  PortSettingLiveWord("Vertical sync", &fSync);
  const std::string speed = "Speed: " + PortSettingLiveWord("Game speed", &fSpeed);
  const char *pszSync = fSync >= 0.5f ? "Vertical sync: on" : "Vertical sync: off";
  const char *pszMode = g_nMode == VIEW_WINDOW ? "View: window" : g_nMode == VIEW_FULL ? "View: full" : "View: counter";
  const std::string close = "Close" + KeyNote();
  const char *apsz[6] = {"Restart clock", frame.c_str(), pszSync, speed.c_str(), pszMode, close.c_str()};
  const Action aAct[6] = {ACT_CLOCK, ACT_FRAME_RATE, ACT_VSYNC, ACT_SPEED, ACT_MODE, ACT_CLOSE};
  float fRight = fUnitsW - kMargin - 10.0f;
  float afX[6];
  for (int i = 5; i >= 0; --i) {
    fRight -= ButtonWidth(p, apsz[i]);
    afX[i] = fRight;
    fRight -= 8.0f;
  }
  for (int i = 0; i < 6; ++i)
    AddButton(p, afX[i], 13.0f, apsz[i], aAct[i]);
}

void ClockColumn(const Pen &p, float x, float y, float h, const PortClockNow &c, const float *pfGame, int nGame)
{
  Panel(p, x, y, kColumn, h);
  x += 14.0f;
  const float w = kColumn - 28.0f, half = (w - 8.0f) / 2.0f;
  Heading(p, x, y + 26.0f, "GAME CLOCK");
  char a[6][48];
  snprintf(a[0], 48, "%.2f", c.fPacePerS);
  snprintf(a[1], 48, "%.2f", c.fNominalMs > 0.0 ? 1000.0 / c.fNominalMs : 0.0);
  snprintf(a[2], 48, "%.0f", c.fDriftMs);
  snprintf(a[3], 48, c.nRecent ? "%.1f \xc2\xb1 %.1f" : "-", c.fMeanMs, c.fSdMs);
  snprintf(a[4], 48, c.nRecent ? "%.0f %%" : "-", c.fWithinPct);
  if (c.nRecent && c.nShownLeast >= 0 && c.nShownLeast != c.nShownMax)
    snprintf(a[5], 48, "%d (%d-%d)", c.nShownMost, c.nShownLeast, c.nShownMax);
  else
    snprintf(a[5], 48, c.nRecent ? "%d" : "-", c.nShownMost);
  const bool bPaceOk = c.fNominalMs > 0.0 && SDL_fabs(c.fPacePerS - 1000.0 / c.fNominalMs) < 0.05 * 1000.0 / c.fNominalMs;
  float fy = y + 40.0f;
  Figure(p, x, fy, half, "Game frames a second", a[0], c.nRecent ? (bPaceOk ? kGood : kBad) : kInk);
  Figure(p, x + half + 8.0f, fy, half, "Due a second", a[1]);
  fy += 60.0f;
  Figure(p, x, fy, half, "Drift ms", a[2]);
  Figure(p, x + half + 8.0f, fy, half, "Between frames ms", a[3]);
  fy += 60.0f;
  Figure(p, x, fy, half, "Within 2 ms of due", a[4], c.nRecent ? (c.fWithinPct >= 90.0 ? kGood : kBad) : kInk);
  Figure(p, x + half + 8.0f, fy, half, "Shown a game frame", a[5]);
  fy += 66.0f;
  // the histogram of the last game frames' times, in whole ms from half the due time to 1.75 of it
  Text(p, x, fy + 10.0f, 12.0f, kDim, "time between game frames, the last 20 s");
  fy += 18.0f;
  const float fHistH = 110.0f;
  UiRing(x * p.k, fy * p.k, w * p.k, fHistH * p.k, UiRgb(kLine), 3.0f * p.k, 1.0f);
  const int nLo = static_cast<int>(c.fNominalMs * 0.5), nHi = static_cast<int>(c.fNominalMs * 1.75 + 1.0);
  const int nBins = nHi - nLo + 1;
  if (nBins > 1 && nBins < 512) {
    int anBins[512] = {};
    int nTop = 1;
    for (int i = 0; i < nGame; ++i) {
      const int b = static_cast<int>(pfGame[i] + 0.5f) - nLo;
      if (b >= 0 && b < nBins)
        nTop = SDL_max(nTop, ++anBins[b]);
    }
    const float bw = (w - 4.0f) / nBins;
    for (int b = 0; b < nBins; ++b)
      if (anBins[b] > 0) {
        const float bh = anBins[b] * (fHistH - 8.0f) / nTop;
        UiFill((x + 2.0f + b * bw) * p.k, (fy + fHistH - 3.0f - bh) * p.k, SDL_max(1.0f, bw - 1.0f) * p.k, bh * p.k,
               UiRgb(kGfps));
      }
    const float fDue = x + 2.0f + (static_cast<float>(c.fNominalMs) - nLo + 0.5f) * bw;
    UiFill((fDue - 1.0f) * p.k, fy * p.k, 2.0f * p.k, fHistH * p.k, UiRgb(kBad));
    char sz[24];
    snprintf(sz, sizeof sz, "%d ms", nLo);
    Text(p, x + 5.0f, fy + 15.0f, 11.0f, kDim, sz);
    snprintf(sz, sizeof sz, "%d ms", nHi);
    Text(p, x + w - 5.0f - TextWidth(p, 11.0f, sz), fy + 15.0f, 11.0f, kDim, sz);
  }
  fy += fHistH + 22.0f;
  char sz[160];
  if (c.nRecent) {
    snprintf(sz, sizeof sz, "due every %.1f ms; between %.1f and %.1f", c.fNominalMs, c.fLeastMs, c.fMostMs);
    Text(p, x, fy, 12.0f, kDim, sz);
    fy += 18.0f;
  }
  snprintf(sz, sizeof sz, "%u game frames timed, %u pauses or holds", c.nFrames, c.nGaps);
  Text(p, x, fy, 12.0f, kDim, sz);
  fy += 30.0f;
  Heading(p, x, fy, "SETTINGS");
  fy += 22.0f;
  const char *const apszRows[] = {"Game frame timing", "Frame rate", "Vertical sync", "Game speed", "Smooth minimap drag"};
  for (const char *pszRow : apszRows) {
    float f = 0.0f;
    std::string word = PortSettingLiveWord(pszRow, &f);
    if (word.empty())
      continue;
    Text(p, x, fy, 13.0f, kDim, pszRow);
    Text(p, x + w - TextWidth(p, 13.0f, word.c_str()), fy, 13.0f, kInk, word.c_str());
    fy += 20.0f;
  }
  (void)h;
}

void PerfColumn(const Pen &p, float x, float y, float h)
{
  Panel(p, x, y, kColumn, h);
  x += 14.0f;
  const float w = kColumn - 28.0f, half = (w - 8.0f) / 2.0f;
  Heading(p, x, y + 26.0f, "PERFORMANCE");
  // at the column's foot, the two copy buttons: the live guide's address, and the context an AI
  // assistant needs to use the channel (port_control.cpp, PortControlAiContext)
  Heading(p, x, y + h - 54.0f, "LIVE CHANNEL");
  const bool bCopied = g_nCopied != ACT_NONE && SDL_GetTicks() - g_nCopiedAt < 2000;
  const char *pszLink = bCopied && g_nCopied == ACT_COPY_LINK ? "Copied" : "Copy guide link";
  const char *pszAi = bCopied && g_nCopied == ACT_COPY_AI ? "Copied" : "Copy for AI";
  const float fLinkW = ButtonWidth(p, "Copy guide link"), fAiW = ButtonWidth(p, "Copy for AI");
  AddButton(p, x, y + h - 44.0f, pszLink, ACT_COPY_LINK, bCopied && g_nCopied == ACT_COPY_LINK, fLinkW);
  AddButton(p, x + fLinkW + 8.0f, y + h - 44.0f, pszAi, ACT_COPY_AI, bCopied && g_nCopied == ACT_COPY_AI, fAiW);
  PortBenchSecond secs[60];
  double dPresentMs = 0.0;
  const int n = PortPerfLiveSeconds(secs, &dPresentMs, 60);
  char a[4][32];
  if (n > 0) {
    const PortBenchSecond &s = secs[n - 1];
    snprintf(a[0], 32, "%.0f", s.dFramesPerS);
    snprintf(a[1], 32, "%.1f", s.dGameFramesPerS);
    snprintf(a[2], 32, "%u", s.nUnits);
    snprintf(a[3], 32, "%.2f", dPresentMs);
  } else {
    for (auto &s : a)
      strcpy(s, "-");
  }
  float fy = y + 40.0f;
  Figure(p, x, fy, half, "Frames a second", a[0]);
  Figure(p, x + half + 8.0f, fy, half, "Game frames a second", a[1]);
  fy += 60.0f;
  Figure(p, x, fy, half, "Units", a[2]);
  Figure(p, x + half + 8.0f, fy, half, "Present ms", a[3]);
  fy += 66.0f;
  // the last minute, frames shown and game frames a second, each on its own scale
  Text(p, x, fy + 10.0f, 12.0f, kDim, "the last minute, a second at a time");
  fy += 18.0f;
  float afFps[60], afGfps[60], fTopFps = 1.0f, fTopG = 1.0f;
  for (int i = 0; i < n; ++i) {
    afFps[i] = static_cast<float>(secs[i].dFramesPerS);
    afGfps[i] = static_cast<float>(secs[i].dGameFramesPerS);
    fTopFps = SDL_max(fTopFps, afFps[i] * 1.15f);
    fTopG = SDL_max(fTopG, afGfps[i] * 1.15f);
  }
  Plot(p, x, fy, w, 90.0f, afFps, n, fTopFps, kFps, 0.0f, 0);
  Plot(p, x, fy, w, 90.0f, afGfps, n, fTopG, kGfps, 0.0f, 0);
  fy += 108.0f;
  // the parts, ms a game frame over the last five seconds
  Text(p, x, fy, 12.0f, kDim, "a game frame's parts, ms, the last 5 s");
  fy += 12.0f;
  double adAvg[PERF_PARTS] = {};
  const int nAvg = SDL_min(n, 5);
  double dMost = 0.01;
  for (int i = 0; i < PERF_PARTS; ++i) {
    for (int k = n - nAvg; k < n; ++k)
      adAvg[i] += secs[k].adPartMs[i] / nAvg;
    dMost = SDL_max(dMost, adAvg[i]);
  }
  for (int i = 0; i < PERF_PARTS; ++i) {
    fy += 19.0f;
    Text(p, x, fy, 12.0f, kInk, PortPerfPartName(i));
    const float bx = x + 70.0f, bw = w - 70.0f - 52.0f;
    UiFill(bx * p.k, (fy - 8.0f) * p.k, bw * p.k, 7.0f * p.k, UiRgb(kLine), 2.0f * p.k);
    UiFill(bx * p.k, (fy - 8.0f) * p.k, static_cast<float>(bw * adAvg[i] / dMost) * p.k, 7.0f * p.k, UiRgb(kParts[i]),
           2.0f * p.k);
    char sz[24];
    snprintf(sz, sizeof sz, "%.3f", adAvg[i]);
    Text(p, x + w - TextWidth(p, 12.0f, sz), fy, 12.0f, kDim, sz);
  }
  (void)h;
}

void Foot(const Pen &p, float x, float y, float w, float h, const PortClockNow &c, const float *pfGame, int nGame,
          const float *pfShown, int nShown)
{
  Panel(p, x, y, w, h);
  x += 14.0f;
  w -= 28.0f;
  const float half = (w - 16.0f) / 2.0f;
  Heading(p, x, y + 26.0f, "FRAMES SHOWN, MS EACH");
  Heading(p, x + half + 16.0f, y + 26.0f, "GAME FRAMES, MS EACH");
  float fMean = 0.0f;
  for (int i = 0; i < nShown; ++i)
    fMean += pfShown[i] / SDL_max(1, nShown);
  const float fTopShown = SDL_max(20.0f, fMean * 2.5f);
  Plot(p, x, y + 38.0f, half, h - 52.0f, pfShown, nShown, fTopShown, kFps, fMean, kDim);
  Plot(p, x + half + 16.0f, y + 38.0f, half, h - 52.0f, pfGame, nGame, static_cast<float>(c.fNominalMs * 1.75), kGfps,
       static_cast<float>(c.fNominalMs), kBad);
  char sz[64];
  snprintf(sz, sizeof sz, "%.0f ms", fTopShown);
  Text(p, x + 6.0f, y + 54.0f, 11.0f, kDim, sz);
  snprintf(sz, sizeof sz, "%.0f ms, due %.0f", c.fNominalMs * 1.75, c.fNominalMs);
  Text(p, x + half + 22.0f, y + 54.0f, 11.0f, kDim, sz);
}

void Counter(const Pen &p, const PortClockNow &c)
{
  PortBenchSecond secs[1];
  const int n = PortPerfLiveSeconds(secs, NULL, 1);
  char sz[128];
  snprintf(sz, sizeof sz, "%.0f fps  \xc2\xb7  %.1f game fps  \xc2\xb7  %.1f \xc2\xb1 %.1f ms", n ? secs[0].dFramesPerS : 0.0,
           c.fPacePerS, c.fMeanMs, c.fSdMs);
  const float w = TextWidth(p, 13.0f, sz) + 20.0f;
  const float x = g_gameX / p.k + g_gameW / p.k - w - 10.0f, y = g_gameY / p.k + 10.0f;
  const bool bHot = g_nHot == 0;
  UiFill(x * p.k, y * p.k, w * p.k, 28.0f * p.k, UiRgb(bHot ? kLine : 0x000000, 0.6f), 4.0f * p.k);
  Text(p, x + 10.0f, y + 19.0f, 13.0f, kInk, sz);
  // a click closes it
  g_buttons[g_nButtons++] = {x * p.k, y * p.k, w * p.k, 28.0f * p.k, ACT_CLOSE};
}

// While the overlay is off: a small button at the top of the game, in the middle, that opens it.
void Opener(const Pen &p)
{
  // it says the channel is open, which a mod may have opened, so the player knows tools can reach the game
  const std::string label = std::string(PortControlRunning() ? "Live channel open \xC2\xB7 Overlay" : "Overlay") + KeyNote();
  const float w = ButtonWidth(p, label.c_str());
  const float x = (g_gameX + g_gameW / 2.0f) / p.k - w / 2.0f, y = g_gameY / p.k + 6.0f;
  AddButton(p, x, y, label.c_str(), ACT_OPEN);
}

void Act(Action act)
{
  std::string answer;
  float f = 0.0f;
  switch (act) {
    case ACT_CLOCK: PortClockStart(); break;
    case ACT_FRAME_RATE:
      PortSettingLiveWord("Frame rate", &f);
      PortSettingLiveSet("Frame rate", (static_cast<int>(f) + 1) % 6, -1, &answer);
      break;
    case ACT_VSYNC:
      PortSettingLiveWord("Vertical sync", &f);
      PortSettingLiveSet("Vertical sync", f >= 0.5f ? 0.0 : 1.0, -1, &answer);
      break;
    case ACT_SPEED:
      PortSettingLiveWord("Game speed", &f);
      PortSettingLiveSet("Game speed", (static_cast<int>(f) + 1) % 7, -1, &answer);
      break;
    case ACT_MODE: PortDebugViewCycle(); break;
    case ACT_CLOSE: PortDebugViewSet(VIEW_OFF); break;
    case ACT_OPEN: PortDebugViewSet(VIEW_WINDOW); break;
    case ACT_COPY_LINK:
    case ACT_COPY_AI: {
      const int nPort = PortControlPort() > 0 ? PortControlPort() : PORT_CONTROL_PORT;
      // the link holds the channel's key, which the guide's page passes on to its WebSocket
      const std::string text = act == ACT_COPY_LINK
                                   ? "http://127.0.0.1:" + std::to_string(nPort) + "/guide?t=" + PortControlKey()
                                   : PortControlAiContext(true);
      if (SDL_SetClipboardText(text.c_str())) {
        g_nCopied = act;
        g_nCopiedAt = SDL_GetTicks();
      }
      break;
    }
    default: break;
  }
  PortPresentRedraw();
}

}  // namespace

bool PortDebugViewAvailable(void)
{
  // a developer's run; else while the channel runs (a tool mod, the canvas), but not over a
  // campaign's mission, which no mod reaches (PRD 2026-10-27)
  if (PortCommandOptions()->szControl[0] != '\0' || SDL_getenv("HYPERION_DEBUG_VIEW") != NULL)
    return true;
  return PortControlRunning() && !(gwGameMode == 3 && RulesCampaignMission() != 0);
}

int PortDebugViewMode(void)
{
  return g_nMode;
}

const char *PortDebugViewModeName(void)
{
  return kModeNames[g_nMode];
}

void PortDebugViewSet(int nMode)
{
  g_nMode = nMode >= 0 && nMode < VIEW_MODES ? nMode : VIEW_OFF;
  // the readings it shows: the clock starts with it, and stays on for the panel when it closes
  if (g_nMode != VIEW_OFF && !PortClockOn())
    PortClockStart();
  g_nButtons = 0;
  g_nHot = -1;
  g_bDrawn = false;
  PortPresentRedraw();
}

void PortDebugViewCycle(void)
{
  PortDebugViewSet((g_nMode + 1) % VIEW_MODES);
}

// in a game, and between the two games of a map switch, so that the overlay stays up through
// the switch with its game's box black
bool PortDebugViewShown(void)
{
  // PortDebugViewAvailable: one turned on in a skirmish stays off through a campaign's mission
  return g_nMode != VIEW_OFF && (gwGameMode == 3 || PortMotionCovered()) && !PortBenchViewShown() &&
         PortDebugViewAvailable();
}

static void LayerArea(float fW, float fH, float *pfX, float *pfY, float *pfW, float *pfH)
{
  if (!PortDebugViewShown() || fW <= 0.0f || fH <= 0.0f) {
    *pfX = *pfY = 0.0f;
    *pfW = fW;
    *pfH = fH;
    return;
  }
  const Layout l = LayoutFor(fW, fH);
  *pfX = l.areaX;
  *pfY = l.areaY;
  *pfW = l.areaW;
  *pfH = l.areaH;
}

// True when the overlay was drawn; the Overlay button alone does not count.
static bool LayerDraw(int nWidth, int nHeight, float fX, float fY, float fW, float fH)
{
  g_gameX = fX;
  g_gameY = fY;
  g_gameW = fW;
  g_gameH = fH;
  g_nButtons = 0;
  g_bDrawn = PortDebugViewShown();
  // the Overlay button: whenever the channel is open (the canvas, a developer's run, a tool mod),
  // as its sign; HYPERION_CAPTURE, a capture for the website (tools/capture/), shows the game without it
  const bool bOpenerHere = PortControlRunning() || PortLaunchIsCanvas() || PortCommandOptions()->szControl[0] != '\0' ||
                           SDL_getenv("HYPERION_DEBUG_VIEW") != NULL;
  g_bOpener = !g_bDrawn && g_nMode == VIEW_OFF && gwGameMode == 3 && PortDebugViewAvailable() && bOpenerHere &&
              SDL_getenv("HYPERION_CAPTURE") == NULL;
  if (g_bOpener) {
    const Pen p = {StageScale(static_cast<float>(nWidth), static_cast<float>(nHeight)), 0.7f};
    Opener(p);
    return false;
  }
  if (!g_bDrawn)
    return false;
  const Layout l = LayoutFor(static_cast<float>(nWidth), static_cast<float>(nHeight));
  const Pen p = {l.k, g_nMode == VIEW_WINDOW ? 0.96f : 0.62f};
  static float s_afGame[480], s_afShown[600];
  PortClockNow c;
  PortClockRead(&c, 480, s_afGame, 480, s_afShown, 600);
  if (g_nMode == VIEW_COUNTER) {
    Counter(p, c);
    return true;
  }
  TopStrip(p, l.fUnitsW, c);
  const float fColumnY = kTop + kMargin, fColumnH = l.fUnitsH - fColumnY - kMargin;
  ClockColumn(p, kMargin, fColumnY, fColumnH, c, s_afGame, c.nGameGot);
  PerfColumn(p, l.fUnitsW - kMargin - kColumn, fColumnY, fColumnH);
  const float fFootX = kMargin * 2.0f + kColumn;
  Foot(p, fFootX, l.fUnitsH - kMargin - kFoot, l.fUnitsW - 2.0f * fFootX, kFoot, c, s_afGame, c.nGameGot, s_afShown,
       c.nShownGot);
  return true;
}

static bool LayerOutside(float fX, float fY)
{
  return g_nMode == VIEW_WINDOW && g_bDrawn && PortDebugViewShown() &&
         (fX < g_gameX || fY < g_gameY || fX >= g_gameX + g_gameW || fY >= g_gameY + g_gameH);
}

static bool LayerMouse(unsigned uMsg, unsigned long long wParam, long long lParam)
{
  (void)wParam;
  if (gwGameMode != 3 || !((PortDebugViewShown() && g_bDrawn) || g_bOpener))
    return false;
  const float fX = static_cast<float>(static_cast<short>(LOWORD(lParam)));
  const float fY = static_cast<float>(static_cast<short>(HIWORD(lParam)));
  int nHit = -1;
  for (int i = 0; i < g_nButtons; ++i)
    if (fX >= g_buttons[i].x && fY >= g_buttons[i].y && fX < g_buttons[i].x + g_buttons[i].w &&
        fY < g_buttons[i].y + g_buttons[i].h)
      nHit = i;
  if (nHit != g_nHot) {
    g_nHot = nHit;
    PortPresentRedraw();
  }
  if (nHit >= 0) {
    if (uMsg == WM_LBUTTONDOWN)
      Act(g_buttons[nHit].act);
    // a button's press and release are the overlay's; a release ends a drag the game began
    return uMsg != WM_LBUTTONUP && uMsg != WM_RBUTTONUP && uMsg != WM_MBUTTONUP;
  }
  // round the window, the game sees nothing but the releases of what it saw pressed
  if (LayerOutside(fX, fY))
    return uMsg != WM_LBUTTONUP && uMsg != WM_RBUTTONUP && uMsg != WM_MBUTTONUP;
  return false;
}

// The overlay as a layer over the game (port_overlay.h): the cursor free to reach its panels,
// and up between the games of a map switch too.
const PortOverlayLayer kLayer = {
    "debug", PORT_OVERLAY_DEBUG, PORT_OVERLAY_FREE_CURSOR | PORT_OVERLAY_OVER_COVER,
    PortDebugViewShown, LayerArea, LayerDraw, NULL, LayerMouse, NULL, LayerOutside};
PORT_OVERLAY_LAYER(kLayer);
