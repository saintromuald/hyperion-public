// port_benchview.cpp -- a benchmark run's screen: the game in a window of its own
// with the overlay round it, covering no
// part of the game, or filling the screen with the same panels see-through over it. The top
// strip and its keys, the camera's three ways (C), the minimap a click moves, the numbers a
// second and a game frame by part against the earlier run, the frame-time graph, H's counter
// and the end summary are as docs/modules/tools.md, "The benchmark", describes them.
//
// Presentation only: the run is a replay, and nothing here reaches the game but the camera and
// the pause. It is a layer over the game (port_overlay.h): the GPU presenter places the game in
// its area and draws it after the game, and the mouse over the panels reaches it, not the game.
#include <windows.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <algorithm>
#include <string>
#include <vector>
#include <SDL3/SDL.h>
#include "port_bench.h"
#include "port_benchview.h"
#include "../render/port_camera.h"  // PortCameraZoomTo, PortCameraZoomMax
#include "../render/port_console.h"  // PortWireframeCell
#include "../loop/port_perf.h"  // PERF_PARTS
#include "../render/port_present.h"  // PortPresentRedraw
#include "../replays/port_tv.h"  // PortTvOn, PortTvToggle
#include "../render/port_view.h"  // PortViewCameraCentre, PortViewScreenToMap, g_portView
#include "../platform/port_util.h"  // PortReadText
#include "../front/port_frontkit.h"   // the theme's colours
#include "../front/port_frontlobby.h" // MapPictureRead
#include "../platform/port_json.h"
#include "../render/port_motion.h"     // PortMotionCovered
#include "../render/port_overlay.h"    // PORT_OVERLAY_LAYER
#include "../render/port_scene.h"      // PortSceneStage, SCENE_SCREEN
#include "../front/port_ui.h"
#include "CUnit.h"
#include "glues.h"           // mem_free
#include "maphdr.h"          // get_map_chk
#include "TrigAct.h"         // pause_game, unpause_game
#include "../gameplay/port_ai.h"         // AiLevelsLabel, AiLevelName, AiProfileOf
#include "port_benchinfo.h"  // the selection and the players
#include "port_benchoverlay.h"
#include "CUnitMisc.h"       // get_unit_name_from_type
#include "playerlimits.h"    // PLAYER_SLOTS_MAX
#include "../../gen/globals117.h"
#include "../platform/port_globals117.h"

using namespace FrontKit;

namespace {

const char *const kTimes = " \xc3\x97 ";
const char *const kDash = "\xe2\x80\x94";

// bench.css's, and the design's 32 player colours in the engine's order
const unsigned kGood = 0x7bd88f, kBad = 0xff8a65, kThis = 0x66d5fe, kVs = 0xffe490, kGold = 0xffe490;
const unsigned kColours[32] = {0xf40404, 0x0c48cc, 0x2cb494, 0x88409c, 0xf88c14, 0x703014, 0xcce0d0, 0xfcfc38,
                               0x10fc18, 0x7cacfc, 0x00e4fc, 0x006400, 0x4874b4, 0xfcfc00, 0x683078, 0xbca45c,
                               0xfccc2c, 0x486c50, 0x18acb8, 0x088008, 0xffffff, 0x2424fc, 0xfc9468, 0xbc6824,
                               0xf40404, 0xac8c7c, 0x004400, 0xecc4b0, 0x4068d4, 0x344c6c, 0x58b058, 0x7c5050};
const unsigned kParts[PERF_PARTS] = {0x8fb8de, 0xb0705a, 0xe6b450, 0x6f8fbf, 0x4fc3d9, 0x7bd88f,
                                     0xc792ea, 0x9aa6b2, 0x5fb3a1, 0xd6d26b, 0xf07178};
const char *const kSpeeds[7] = {PT_("Slowest"), PT_("Slower"), PT_("Slow"),  PT_("Normal"),
                                PT_("Fast"),    PT_("Faster"), PT_("Fastest")};

const int kMinimapSlot = 14;  // the picture atlas's, beside the set-up's 13

enum { SHOWN_WINDOW, SHOWN_FULL };
enum { CAM_ACTION, CAM_MAP, CAM_FREE, CAM_WAYS };
enum { GRAPH_TIME, GRAPH_PARTS, GRAPH_UNITS };
const char *const kCameras[CAM_WAYS] = {PT_("Follow the action"), PT_("The whole map"), PT_("Free")};

int g_nShown = SHOWN_WINDOW;
bool g_bHidden;      // H: the overlay off, a counter left
int g_nCamera = CAM_ACTION;
bool g_bCameraSet;   // the camera's way applied to this game
int g_nGraph = GRAPH_TIME;
bool g_bSummary;     // the summary over the game, at the run's end
bool g_bSummaryClosed;
bool g_bAskLeave;    // "Stop the benchmark?"
unsigned g_nGameFrom = ~0u;  // the frame the overlay's game started at, to see a new game

// The run's shown mode: its window, or full screen; no picture is the window with nothing in it.
bool NoPicture(void)
{
  return PortBenchCurrent()->nShown == 2;
}

void ShownNow(void)
{
  g_nShown = PortBenchCurrent()->nShown == 1 ? SHOWN_FULL : SHOWN_WINDOW;
}

std::string PictureName(void)
{
  return PortBenchCurrent()->nPicture == 1 ? PortText("Classic 256 colours") : "GPU";
}

// The stage: 1080 units high and at least 1920 wide, as port_ui.cpp's.
float StageScale(float fW, float fH)
{
  return fW / fH >= 1920.0f / 1080.0f ? fH / 1080.0f : fW / 1920.0f;
}

struct Rect {
  float x, y, w, h;
  bool In(float px, float py) const { return px >= x && py >= y && px < x + w && py < y + h; }
};

// Where everything goes, in stage units: bench.js's at 1920 by 1080, the window in the middle.
struct Layout {
  float k, fW, fH;
  Rect top, frame, game, left, right, foot, mini, sel;
  bool bMiniBox;  // full screen: the minimap in a box of its own
};

bool LayoutShows(unsigned nBits)
{
  return (PortBenchOverlayShows(PortBenchOverlayLive()) & nBits) != 0 && PortBenchCurrent()->nShown != 2;
}

Layout LayoutFor(float fW, float fH)
{
  Layout l;
  l.k = StageScale(fW, fH);
  l.fW = fW / l.k;
  l.fH = fH / l.k;
  l.top = {16.0f, 13.0f, l.fW - 32.0f, 59.0f};
  l.bMiniBox = g_nShown == SHOWN_FULL;
  if (g_nShown == SHOWN_WINDOW) {
    const float fFootH = 209.0f;
    l.foot = {16.0f, l.fH - 16.0f - fFootH, l.fW - 32.0f, fFootH};
    const float fFrameW = 1285.0f;
    l.frame = {SDL_floorf((l.fW - fFrameW) / 2.0f), 92.0f, fFrameW, l.foot.y - 10.0f - 92.0f};
    l.game = {l.frame.x + 2.5f, l.frame.y + 31.0f, 1280.0f, l.frame.h - 31.0f - 2.5f};
    l.left = {16.0f, 92.0f, l.frame.x - 16.0f - 12.0f, l.frame.h};
    const float fRightX = l.frame.x + l.frame.w + 12.0f;
    l.right = {fRightX, 92.0f, l.fW - 16.0f - fRightX, l.frame.h};
    l.mini = {0, 0, 0, 0};  // at the left panel's foot, placed as it is drawn
    // the selection at the foot's right, the graph in the rest
    l.sel = {0, 0, 0, 0};
    if (LayoutShows(PORT_OV_SELECTION)) {
      l.sel = {l.fW - 16.0f - 648.0f, l.foot.y, 648.0f, fFootH};
      l.foot.w = l.sel.x - 12.0f - 16.0f;
    }
  } else {
    // Full overlay: the players down the left, the figures down the right, along the foot the
    // graph, the selection and the minimap (bench.js's ovRects, times 0.9)
    l.frame = l.game = {0.0f, 0.0f, l.fW, l.fH};
    const float fFootH = 207.0f;
    l.mini = {l.fW - 16.0f - 270.0f, l.fH - 16.0f - 270.0f, 270.0f, 270.0f};
    const float fFootY = l.fH - 16.0f - fFootH;
    const float fRowEnd = LayoutShows(PORT_OV_MINIMAP) ? l.mini.x - 12.0f : l.fW - 16.0f;
    l.sel = {0, 0, 0, 0};
    if (LayoutShows(PORT_OV_SELECTION))
      l.sel = {fRowEnd - 648.0f, fFootY, 648.0f, fFootH};
    l.foot = {16.0f, fFootY, (l.sel.w > 0.0f ? l.sel.x - 12.0f : fRowEnd) - 16.0f, fFootH};
    l.left = {16.0f, 90.0f, 387.0f, fFootY - 12.0f - 90.0f};
    l.right = {l.fW - 16.0f - 324.0f, 90.0f, 324.0f,
               SDL_min(576.0f, (LayoutShows(PORT_OV_MINIMAP) ? l.mini.y : fFootY) - 12.0f - 90.0f)};
  }
  return l;
}

// Drawing in stage units.
struct Pen {
  float k;
  bool bOver;  // full screen: the panels see-through over the game
};

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

// Text cut at w units with an ellipsis.
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

// A panel: bench.css's --bm-panel, see-through over the game at full screen.
// The overlay's panels as drawn, target pixels: over them the pointer is off the game, and
// Windows' arrow shows above the overlay (LayerOutside).
Rect g_covers[16];
int g_nCovers;

void AddCover(const Pen &p, const Rect &r)
{
  if (g_nCovers < static_cast<int>(sizeof g_covers / sizeof g_covers[0]))
    g_covers[g_nCovers++] = {r.x * p.k, r.y * p.k, r.w * p.k, r.h * p.k};
}

// see-through over the game: rgba(3,12,24) at 0.04 + 0.92 x (1 - see), the design's
float PanelAlpha(void)
{
  const float fSee = PortBenchOverlaySee(PortBenchOverlayLive(), PortBenchCurrent()->nShown) / 100.0f;
  return 0.04f + 0.92f * (1.0f - fSee);
}

void Panel(const Pen &p, const Rect &r)
{
  AddCover(p, r);
  Fill(p, r, p.bOver ? UiRgb(0x030c18, PanelAlpha()) : UiRgb(0x040e1c, 0.82f), 3.6f);
  Ring(p, r, kLbInnerRim, 3.6f, 1.8f);
}

void Title(const Pen &p, float x, float y, const char *psz)
{
  Text(p, x, y, 13.5f, kLbInkInfo, psz, UI_FACE_WIDE);
}

// The buttons of the last frame drawn, in target pixels.
enum Action {
  ACT_NONE, ACT_GO, ACT_PAUSE, ACT_RESTART, ACT_HIDE, ACT_SETUP, ACT_CAMERA, ACT_GRAPH, ACT_MINIMAP,
  ACT_AGAIN, ACT_RUNS, ACT_CLOSE, ACT_LEAVE, ACT_KEEP, ACT_SHOW, ACT_OVERLAY, ACT_OVSET, ACT_OVUSE, ACT_OVCANCEL,
  ACT_PORTRAIT, ACT_PLAYER, ACT_LEFTTAB, ACT_SLOWER, ACT_FASTER
};
struct Button {
  Rect r;  // target pixels
  Action act;
  int n;
};
Button g_buttons[64];
int g_nButtons;
int g_nHot = -1;
Rect g_gameBox;  // the game's box, target pixels
bool g_bDrawn;
Rect g_miniBox;  // the minimap's square, stage units, as drawn
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

enum Look { LOOK_PLAIN, LOOK_GO, LOOK_STOP, LOOK_PRESSED, LOOK_GOLD };

// A button as the design's sc-mini: its label, its hot key's letter underlined. fMinW keeps a
// button whose label changes at one size; bOff greys it and takes no click.
float Button(const Pen &p, float x, float y, float h, float fSize, const char *psz, Action act, Look look = LOOK_PLAIN,
             bool bOff = false, float fMinW = 0.0f, int n = 0, char cKey = 0)
{
  const float w = SDL_max(Width(p, fSize, psz, UI_FACE_BOLD) + 2.0f * 14.0f, fMinW);
  const Rect r = {x, y, w, h};
  const bool bHot = !bOff && Hot(act, n);
  const float fAlpha = bOff ? 0.45f : 1.0f;
  UiColor fill = bHot ? kLbItemHi : kLbItem;
  UiColor rim = kLbItemRim, ink = kLbInkInfo;
  if (look == LOOK_GO) {
    rim = kLbHyperionRim;
    ink = kScInkAccept;
  } else if (look == LOOK_STOP) {
    rim = UiRgb(kBad);
    ink = UiRgb(kBad);
  } else if (look == LOOK_PRESSED) {
    rim = UiRgb(kGold, 0.8f);
    fill = UiRgb(0x5a4a1a, 0.55f);
    ink = UiRgb(kGold);
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
  const float fTextW = Width(p, fSize, psz, UI_FACE_BOLD);
  const float tx = x + (w - fTextW) / 2.0f, ty = y + h / 2.0f + fSize * 0.36f;
  Text(p, tx, ty, fSize, ink, psz, UI_FACE_BOLD);
  // the hot key: its letter underlined, the first of the label's that is it
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

float ButtonWidth(const Pen &p, float fSize, const char *psz, float fMinW = 0.0f)
{
  return SDL_max(Width(p, fSize, psz, UI_FACE_BOLD) + 2.0f * 14.0f, fMinW);
}

// numbers as bench.js writes them
std::string N0(double d)
{
  char sz[32];
  const long long n = static_cast<long long>(d + (d >= 0 ? 0.5 : -0.5));
  snprintf(sz, sizeof sz, "%lld", n);
  std::string s = sz, out;
  const std::string mark = PortTextC("thousands", ",");
  const size_t nDigits = s.size() - (s[0] == '-' ? 1 : 0);
  for (size_t i = 0; i < s.size(); ++i) {
    out += s[i];
    const size_t nLeft = s.size() - 1 - i;
    if (nLeft > 0 && nLeft % 3 == 0 && nLeft < nDigits)
      out += mark;
  }
  return out;
}

std::string N1(double d)
{
  char sz[32];
  snprintf(sz, sizeof sz, "%.1f", d);
  return sz;
}

std::string N2(double d)
{
  char sz[32];
  snprintf(sz, sizeof sz, "%.2f", d);
  return sz;
}

// game time at Fastest's 23.81 frames a second, and real time, as M:SS
std::string Clock(double dSeconds)
{
  const unsigned n = static_cast<unsigned>(dSeconds < 0 ? 0 : dSeconds);
  char sz[24];
  snprintf(sz, sizeof sz, "%u:%02u", n / 60, n % 60);
  return sz;
}

std::string GameClock(unsigned nFrames)
{
  return Clock(nFrames * 0.042);
}

// ---------------------------------------------------------------------------
// What the overlay shows: the run's settings, its numbers, and the earlier run it is held
// against, read from that run's folder.

std::string MapTitle(void)
{
  const char *pszMap = PortBenchCurrent()->szMap;
  const char *pszName = strrchr(pszMap, '\\');
  pszName = pszName != NULL ? pszName + 1 : pszMap;
  std::string s = pszName;
  const size_t nDot = s.rfind('.');
  if (nDot != std::string::npos)
    s.resize(nDot);
  return s;
}

// the run's speed now, which the replay's Slower and Faster step (PortBenchSpeedStep)
std::string SpeedLabel(void)
{
  int nSpeed, nMul;
  PortBenchSpeedNow(&nSpeed, &nMul);
  if (nMul == 0)
    return PortText("As fast as it goes");
  const std::string speed = PortText(kSpeeds[nSpeed >= 0 && nSpeed < 7 ? nSpeed : 6]);
  return nMul > 1 ? speed + kTimes + std::to_string(nMul) : speed;
}

// the game frames a second the speed asks for, 0 for as fast as it goes
double SpeedRate(void)
{
  int nSpeed, nMul;
  PortBenchSpeedNow(&nSpeed, &nMul);
  if (nMul == 0)
    return 0.0;
  return 1000.0 / PortSpeedFrameMs(nSpeed) * nMul;
}

std::string RacesLabel(void)
{
  const PortBenchSettings &s = *PortBenchCurrent();
  // {0} the race of each slot, as the set-up's --bench-races names them
  if (s.szRaces[0] != '\0')
    return PortTextF("Each slot: {0}", {s.szRaces});
  const std::string race = s.szRace;
  if (race == "terran") return PortText("All Terran");
  if (race == "zerg") return PortText("All Zerg");
  if (race == "protoss") return PortText("All Protoss");
  return PortText("Random, from the seed");
}

std::string LengthLabel(void)
{
  const ULONG dwFrames = PortBenchCurrent()->dwFrames;
  struct { ULONG frames; int minutes; } kLengths[] = {
      {1429, 1}, {7143, 5}, {14286, 10}, {21429, 15}, {42857, 30}, {85714, 60}};
  for (const auto &l : kLengths)
    if (l.frames == dwFrames)
      return PortTextNF("{0} minute", "{0} minutes", l.minutes, {std::to_string(l.minutes)});
  // a custom length, in whole minutes at Fastest's 42 ms a game frame
  const int nMinutes = static_cast<int>(lround(dwFrames * 42.0 / 60000.0));
  if (nMinutes > 0 && static_cast<ULONG>(lround(nMinutes * 60000.0 / 42.0)) == dwFrames) {
    const int h = nMinutes / 60, m = nMinutes % 60;
    std::string s = h > 0 ? PortTextNF("{0} hour", "{0} hours", h, {std::to_string(h)}) : std::string();
    if (m > 0 || h == 0)
      s += (h > 0 ? " " : "") + PortTextNF("{0} minute", "{0} minutes", m, {std::to_string(m)});
    return s;
  }
  return PortTextNF("{0} frame", "{0} frames", dwFrames, {N0(dwFrames)});
}

std::string BuildName(void)
{
  return PortBenchBuild();
}

std::string FolderShown(void)
{
  const std::string folder = PortBenchFolder();
  const size_t nAt = folder.find("benchmarks\\");
  return (nAt == std::string::npos ? folder : folder.substr(nAt)) + "\\";
}

// The earlier run held against: its seconds, and its every shown frame when it kept them.
struct Against {
  std::string path;  // "" for none
  std::string when, build;
  std::string ended, endState;  // its summary's: how it ended and the game's state then
  unsigned nEndFrame = 0;
  bool bRead = false;
  std::vector<float> secFrame, secMs;               // a second's game frame since the start, its mean frame
  std::vector<std::vector<float>> secParts;         // a second's parts, ms a game frame
  std::vector<float> frameMs;                       // every shown frame, with its game frame
  std::vector<unsigned> frameAt;
};
Against g_vs;

std::string JsonWord(const std::string &json, const char *pszKey)
{
  const std::string key = std::string("\"") + pszKey + "\"";
  size_t at = json.find(key);
  if (at == std::string::npos)
    return "";
  at = json.find('"', json.find(':', at) + 1);
  const size_t end = at == std::string::npos ? at : json.find('"', at + 1);
  return end == std::string::npos ? "" : json.substr(at + 1, end - at - 1);
}

void ReadAgainst(void)
{
  const std::string path = PortBenchCurrent()->szAgainst;
  if (g_vs.bRead && g_vs.path == path)
    return;
  g_vs = Against();
  g_vs.bRead = true;
  g_vs.path = path;
  if (path.empty())
    return;
  const std::string settings = PortReadText(path + "\\settings.json");
  g_vs.when = JsonWord(settings, "when");
  g_vs.build = JsonWord(settings, "commit");
  if (g_vs.build.empty())
    g_vs.build = "?";
  {
    PortJson sum;
    PortJsonParse(PortReadText(path + "\\summary.json").c_str(), &sum);
    g_vs.ended = sum["ended"].Str();
    g_vs.endState = sum["end_state"].Str();
    g_vs.nEndFrame = static_cast<unsigned>(sum["end_frame"].Num());
  }
  // seconds.csv: second, frames shown, game frames a second, units, the parts
  const std::string text = PortReadText(path + "\\seconds.csv");
  size_t at = text.find('\n');
  double dLast = 0.0, dFrame = 0.0;
  while (at != std::string::npos && at + 1 < text.size()) {
    const size_t next = text.find('\n', at + 1);
    const std::string line = text.substr(at + 1, next == std::string::npos ? std::string::npos : next - at - 1);
    at = next;
    double d[4 + PERF_PARTS] = {};
    int n = 0;
    for (const char *c = line.c_str(); *c && n < 4 + PERF_PARTS;) {
      d[n++] = atof(c);
      c = strchr(c, ',');
      if (c == NULL)
        break;
      ++c;
    }
    if (n < 4)
      continue;
    dFrame += d[2] * (d[0] - dLast);
    dLast = d[0];
    g_vs.secFrame.push_back(static_cast<float>(dFrame));
    g_vs.secMs.push_back(d[1] > 0.0 ? static_cast<float>(1000.0 / d[1]) : 0.0f);
    std::vector<float> parts(PERF_PARTS, 0.0f);
    for (int i = 0; i < PERF_PARTS && 4 + i < n; ++i)
      parts[i] = static_cast<float>(d[4 + i]);
    g_vs.secParts.push_back(parts);
  }
  // frames.csv: frame_ms, game_frame, with the frame counter as the replay has it: the run's own
  // from its first
  const std::string frames = PortReadText(path + "\\frames.csv");
  at = frames.find('\n');
  unsigned nFirst = ~0u;
  while (at != std::string::npos && at + 1 < frames.size()) {
    const char *c = frames.c_str() + at + 1;
    float fMs = 0.0f;
    unsigned nAt = 0;
    if (sscanf_s(c, "%f,%u", &fMs, &nAt) == 2) {
      if (nFirst == ~0u)
        nFirst = nAt;
      g_vs.frameMs.push_back(fMs);
      g_vs.frameAt.push_back(nAt - nFirst);
    }
    at = frames.find('\n', at + 1);
  }
}

bool HasAgainst(void)
{
  return !g_vs.path.empty() && !g_vs.secFrame.empty();
}

// the other run's second at or before game frame nFrame
int AgainstSecond(unsigned nFrame)
{
  int k = -1;
  for (size_t i = 0; i < g_vs.secFrame.size() && g_vs.secFrame[i] <= nFrame; ++i)
    k = static_cast<int>(i);
  return k < 0 && !g_vs.secFrame.empty() ? 0 : k;
}

// the other run's mean shown frame between two game frames: from its frames when it kept them,
// else its second there
float AgainstMean(unsigned nFrom, unsigned nTo)
{
  if (!g_vs.frameMs.empty()) {
    const auto lo = std::upper_bound(g_vs.frameAt.begin(), g_vs.frameAt.end(), nFrom);
    const auto hi = std::upper_bound(g_vs.frameAt.begin(), g_vs.frameAt.end(), nTo);
    if (hi > lo) {
      double d = 0.0;
      for (auto it = lo; it != hi; ++it)
        d += g_vs.frameMs[it - g_vs.frameAt.begin()];
      return static_cast<float>(d / (hi - lo));
    }
  }
  const int k = AgainstSecond(nTo);
  return k >= 0 ? g_vs.secMs[k] : 0.0f;
}

// the other run's worst 1 % up to game frame nFrame, 0 when it kept no frames
double AgainstWorst(unsigned nFrame)
{
  if (g_vs.frameMs.empty())
    return 0.0;
  const size_t n = std::upper_bound(g_vs.frameAt.begin(), g_vs.frameAt.end(), nFrame) - g_vs.frameAt.begin();
  if (n == 0)
    return 0.0;
  std::vector<float> v(g_vs.frameMs.begin(), g_vs.frameMs.begin() + n);
  const size_t nAt = SDL_min(n * 99 / 100, n - 1);
  std::nth_element(v.begin(), v.begin() + nAt, v.end());
  return v[nAt];
}

// The units: where each is and whose, and the players left, once a frame drawn.
struct Dot {
  float u, v;
  unsigned char player;
  bool bBuilding;
};
std::vector<Dot> g_dots;
int g_nPlayersLeft;

void ReadUnits(void)
{
  g_dots.clear();
  bool abHas[256] = {};
  const float fW = gwMapWidth * 32.0f, fH = gwMapHeight * 32.0f;
  if (fW <= 0.0f || fH <= 0.0f)
    return;
  for (CUnit *pUnit = CUnit::GetUsedList(); pUnit != NULL; pUnit = pUnit->pNext) {
    const int x = pUnit->wPositionX, y = pUnit->wPositionY;
    const unsigned char nPlayer = pUnit->bPlayer;
    abHas[nPlayer] = true;
    g_dots.push_back({x / fW, y / fH, nPlayer, (pUnit->dwFlags & 2) != 0});  // 2: a building on the ground
  }
  // every player with units but the neutral one, 11 in any game
  g_nPlayersLeft = 0;
  for (int i = 0; i < 256; ++i)
    g_nPlayersLeft += abHas[i] && i != 11 ? 1 : 0;
}

// The map's ground, from its file, in the minimap's slot of the picture atlas.
std::string g_minimapOf;
bool g_bMinimap;

void ReadMinimap(void)
{
  const std::string path = PortBenchCurrent()->szMap;
  if (g_minimapOf == path)
    return;
  g_minimapOf = path;
  g_bMinimap = false;
  DWORD dwSize = 0;
  BYTE *pChk = static_cast<BYTE *>(get_map_chk(path.c_str(), &dwSize));
  if (pChk == NULL)
    return;
  MapPicture pic;
  pic.nSlot = kMinimapSlot;
  MapPictureRead(&pic, path, pChk, dwSize);
  mem_free(pChk, __FILE__, __LINE__);
  g_bMinimap = pic.bMinimap;
}

// The camera's way, applied to the game: TV directs a replay's camera to the action
// (port_tv.cpp); the whole map is the most the camera zooms out, centred; free is the viewer's.
// Only the GPU scene zooms (port_view.cpp), so 1.17's 256 colours have no whole map.
bool CameraWayOff(int nWay)
{
  return nWay == CAM_MAP && PortSceneStage() != SCENE_SCREEN;
}

void CameraApply(void)
{
  g_bCameraSet = true;
  if (CameraWayOff(g_nCamera))
    g_nCamera = CAM_ACTION;
  if ((g_nCamera == CAM_ACTION) != (PortTvOn() != 0))
    PortTvToggle();
  if (g_nCamera == CAM_MAP) {
    PortCameraZoomTo(PortCameraZoomMax());
    PortViewCameraCentre(gwMapWidth * 16.0, gwMapHeight * 16.0);
  }
}

void CameraSet(int nWay)
{
  g_nCamera = nWay;
  CameraApply();
  PortPresentRedraw();
}

// ---------------------------------------------------------------------------
// The overlay's parts.

enum State { STATE_LOADING, STATE_RUNNING, STATE_PAUSED, STATE_DONE };

// before the run's first game frame it is loading; after its end, done
State RunState(void)
{
  if (!PortBenchRunning())
    return PortBenchEndReason()[0] != 0 ? STATE_DONE : STATE_LOADING;
  return PortBenchPaused() ? STATE_PAUSED : STATE_RUNNING;
}

void TopStrip(const Pen &p, const Layout &l)
{
  Panel(p, l.top);
  const State st = RunState();
  const std::string ended = PortBenchEndReason();
  const float cy = l.top.y + l.top.h / 2.0f;
  float x = l.top.x + 14.4f;
  // the state
  const char *pszState = st == STATE_LOADING   ? PortText("LOADING")
                         : st == STATE_RUNNING ? PortText("RUNNING")
                         : st == STATE_PAUSED  ? PortText("PAUSED")
                         : ended == "finished" ? PortText("FINISHED")
                         : ended == "last"     ? PortText("ONE LEFT")
                                               : PortText("STOPPED");
  const float fChipW = SDL_max(83.0f, Width(p, 11.7f, pszState, UI_FACE_WIDE) + 18.0f);
  const Rect chip = {x, cy - 13.0f, fChipW, 26.0f};
  if (st == STATE_RUNNING) {
    Fill(p, chip, UiRgb(kGood), 2.7f);
  } else if (st == STATE_PAUSED) {
    Fill(p, chip, UiRgb(kGold), 2.7f);
  } else if (st == STATE_DONE) {
    Ring(p, chip, kLbHyperionRim, 2.7f, 1.0f);
  } else {
    Ring(p, chip, kLbItemRim, 2.7f, 1.0f);
  }
  const UiColor stateInk = st == STATE_DONE      ? UiRgb(kGold)
                           : st == STATE_LOADING ? kLbInkInfo
                                                 : UiRgb(0x000000);
  Text(p, x + (fChipW - Width(p, 11.7f, pszState, UI_FACE_WIDE)) / 2.0f, cy + 4.3f, 11.7f, stateInk, pszState,
       UI_FACE_WIDE);
  x += fChipW + 16.0f;
  // the controls, from the right
  const char *pszGo = st == STATE_DONE ? PortText("Start") : PortText("Stop");
  const char *pszPause = st == STATE_PAUSED ? PortText("Resume") : PortText("Pause");
  const float fBtnH = 38.0f, fSize = 13.5f, fGap = 5.4f;
  const float wGo = ButtonWidth(p, fSize, pszGo, 97.0f), wPause = ButtonWidth(p, fSize, PortText("Resume")),
              wRestart = ButtonWidth(p, fSize, PortText("Restart")),
              wHide = ButtonWidth(p, fSize, PortText("Hide Overlay")),
              wOverlay = ButtonWidth(p, fSize, PortText("Overlay")), wSetup = ButtonWidth(p, fSize, PortText("Set-up")),
              wSlower = ButtonWidth(p, fSize, PortText("Slower")), wFaster = ButtonWidth(p, fSize, PortText("Faster"));
  float bx = l.top.x + l.top.w - 9.0f -
             (wGo + wPause + wSlower + wFaster + wRestart + wOverlay + wHide + wSetup + 7.0f * fGap);
  const float fCtlX = bx;
  const float by = cy - fBtnH / 2.0f;
  bx += Button(p, bx, by, fBtnH, fSize, pszGo, ACT_GO, st == STATE_DONE ? LOOK_GO : LOOK_STOP, st == STATE_LOADING,
               97.0f, 0, 'S') + fGap;
  bx += Button(p, bx, by, fBtnH, fSize, pszPause, ACT_PAUSE, LOOK_PLAIN, st == STATE_DONE || st == STATE_LOADING,
               wPause, 0, 'P') + fGap;
  // the speed, a step at a time (the replay's own buttons and D and U do the same)
  int nSpeed, nMul;
  PortBenchSpeedNow(&nSpeed, &nMul);
  const bool bStill = st == STATE_DONE || st == STATE_LOADING;
  bx += Button(p, bx, by, fBtnH, fSize, PortText("Slower"), ACT_SLOWER, LOOK_PLAIN,
               bStill || (nMul == 1 && nSpeed <= 0), 0.0f, 0, 'D') + fGap;
  bx += Button(p, bx, by, fBtnH, fSize, PortText("Faster"), ACT_FASTER, LOOK_PLAIN, bStill || nMul == 0, 0.0f, 0,
               'U') + fGap;
  bx += Button(p, bx, by, fBtnH, fSize, PortText("Restart"), ACT_RESTART, LOOK_PLAIN, st == STATE_LOADING, 0.0f, 0,
               'R') + fGap;
  bx += Button(p, bx, by, fBtnH, fSize, PortText("Overlay"), ACT_OVERLAY, LOOK_PLAIN, false, 0.0f, 0, 'O') + fGap;
  bx += Button(p, bx, by, fBtnH, fSize, PortText("Hide Overlay"), ACT_HIDE, LOOK_PLAIN, false, 0.0f, 0, 'H') + fGap;
  Button(p, bx, by, fBtnH, fSize, PortText("Set-up"), ACT_SETUP);
  // what runs
  const PortBenchSettings &s = *PortBenchCurrent();
  const std::string what = PortText("Benchmark") + std::string(kDot) + MapTitle();
  // {0} the seed the game was made from, {1} the build it runs on
  const std::string sub = PortTextNF("{0} computer", "{0} computers", s.nPlayers, {std::to_string(s.nPlayers)}) +
                          kDot + SpeedLabel() + kDot + PortTextF("seed {0}", {std::to_string(s.nSeed)}) + kDot +
                          PortTextF("build {0}", {BuildName()});
  const float fWhatW = SDL_min(SDL_max(Width(p, 17.1f, what.c_str(), UI_FACE_BOLD), Width(p, 12.6f, sub.c_str())),
                               (fCtlX - x) * 0.42f);
  TextIn(p, x, cy - 3.0f, fWhatW, 17.1f, kLbInkInfo, what, UI_FACE_BOLD);
  TextIn(p, x, cy + 15.0f, fWhatW, 12.6f, kLbInkDesc, sub);
  x += fWhatW + 16.0f;
  // how far: the bar, then game time, frames and real time
  const unsigned nFrame = PortBenchGameFrame(), nTarget = static_cast<unsigned>(s.dwFrames);
  // {0} the game time now, {1} the game time it runs to; then the frames, then the time passed
  const std::string prog = PortTextF("game {0} of {1}", {GameClock(nFrame), GameClock(nTarget)}) + kDot +
                           PortTextF("frame {0} of {1}", {N0(nFrame), N0(nTarget)}) + kDot +
                           PortTextF("{0} real", {Clock(PortBenchSeconds())});
  const float fProgW = Width(p, 11.7f, prog.c_str());
  const float fBarW = fCtlX - 16.0f - fProgW - 16.0f - x;
  if (fBarW >= 60.0f) {
    const Rect bar = {x, cy - 3.6f, fBarW, 7.2f};
    Fill(p, bar, UiRgb(0x3980bc, 0.2f), 3.6f);
    const float f = nTarget > 0 ? SDL_min(1.0f, static_cast<float>(nFrame) / nTarget) : 0.0f;
    if (f > 0.0f)
      UiGradient(bar.x * p.k, bar.y * p.k, bar.w * f * p.k, bar.h * p.k, kScGlow, UiRgb(kThis), true, 3.6f * p.k);
    x += fBarW + 16.0f;
  }
  if (x + fProgW < fCtlX)
    Text(p, x, cy + 4.3f, 11.7f, UiRgb(0x64d0f9), prog.c_str());
}

// The window's frame round the game, and its title strip with the camera's ways.
void WindowFrame(const Pen &p, const Layout &l, int nPixW, int nPixH)
{
  const Rect &f = l.frame;
  Ring(p, {f.x - 1.0f, f.y - 1.0f, f.w + 2.0f, f.h + 2.0f}, kScRim, 2.0f, 1.8f);
  const Rect bar = {f.x, f.y, f.w, 31.0f};
  UiGradient(bar.x * p.k, bar.y * p.k, bar.w * p.k, bar.h * p.k, kScBarTop, kScBarBot, false, 0.0f);
  Fill(p, {f.x, f.y + 30.0f, f.w, 1.0f}, kScBarEdge);
  // the camera's ways, from the right; with no picture there is no camera
  float bx = f.x + f.w - 5.4f;
  float fCamX = bx;
  if (!NoPicture()) {
    for (int i = CAM_WAYS - 1; i >= 0; --i)
      bx -= ButtonWidth(p, 11.7f, PortText(kCameras[i])) + (i < CAM_WAYS - 1 ? 4.0f : 0.0f);
    fCamX = bx;
    for (int i = 0; i < CAM_WAYS; ++i)
      bx += Button(p, bx, f.y + 3.0f, 25.0f, 11.7f, PortText(kCameras[i]), ACT_CAMERA,
                   g_nCamera == i ? LOOK_PRESSED : LOOK_PLAIN, CameraWayOff(i), 0.0f, i) + 4.0f;
  }
  const std::string size = NoPicture() ? std::string(PortText("no picture"))
                                       : std::to_string(nPixW) + kTimes + std::to_string(nPixH);
  // {0} the renderer's name
  const std::string name = MapTitle() + kDot + PortText("observer") + kDot + size + kDot +
                           PortTextF("{0} renderer", {PictureName()});
  TextIn(p, f.x + 12.6f, f.y + 20.5f, fCamX - f.x - 24.0f, 13.5f, kLbInkInfo, name);
}

void PlayersList(const Pen &p, const Rect &r, float y, bool bNarrow);
bool Shows(unsigned nBits);
int g_nLeftTab;  // 0 the players, 1 the settings and the log

// The players, or the settings and the log; in a window, the minimap at its foot.
void LeftPanel(const Pen &p, const Layout &l)
{
  const Rect &r = l.left;
  Panel(p, r);
  const float x = r.x + 14.4f, w = r.w - 28.8f;
  float y = r.y + 12.6f + 13.5f;
  const bool bPlayers = Shows(PORT_OV_PLAYERS), bSettings = Shows(PORT_OV_SETTINGS) || NoPicture();
  int nTab = bPlayers && bSettings ? g_nLeftTab : bPlayers ? 0 : 1;
  if (bPlayers && bSettings) {
    const char *const kTabs[2] = {PortText("Players"), PortText("Settings and log")};
    float tx = x;
    for (int i = 0; i < 2; ++i)
      tx += Button(p, tx, r.y + 10.0f, 27.0f, 11.7f, kTabs[i], ACT_LEFTTAB, nTab == i ? LOOK_PRESSED : LOOK_PLAIN, false,
                   0.0f, i) + 4.0f;
    y += 34.0f;
  }
  // in a window the minimap at the foot, square
  if (!l.bMiniBox) {
    const float fSide = SDL_min(w, 190.0f);
    if (Shows(PORT_OV_MINIMAP) || NoPicture())
      g_miniBox = {x + (w - fSide) / 2.0f, r.y + r.h - 12.6f - fSide, fSide, fSide};
    else
      g_miniBox = {0, 0, 0, 0};
  }
  if (nTab == 0) {
    Rect list = r;
    if (!l.bMiniBox && g_miniBox.w > 0.0f)
      list.h = g_miniBox.y - 10.0f - r.y;
    PlayersList(p, list, y, r.w < 360.0f);
    return;
  }
  Title(p, x, y, PortText("SETTINGS"));
  y += 10.0f;
  const PortBenchSettings &s = *PortBenchCurrent();
  // {0} how the teams are laid out, as the set-up names it
  const std::pair<const char *, std::string> rows[] = {
      {PortText("Map"), MapTitle()},
      {PortText("Players"), std::to_string(s.nPlayers)},
      {PortText("Races"), RacesLabel()},
      {PortText("Speed"), SpeedLabel()},
      {PortText("Length, at most"), LengthLabel()},
      {PortText("Seed"), std::to_string(s.nSeed)},
      {PortText("Game type"),
       s.nGameType == 1 ? std::string(PortText("Melee, all allied"))
       : s.szTeams[0]   ? PortTextF("Team Match, {0}",
                                    {s.szLayout[0] ? std::string(s.szLayout) : std::string(PortText("teams by hand"))})
       : s.szStarts[0]  ? std::string(PortText("Free For All, starts by slot"))
                        : std::string(PortText("Free For All"))},
      {PortText("Computers"),
       AiLevelsLabel(s.szAiLevels).empty() ? std::string(PortText("Classic AI")) : AiLevelsLabel(s.szAiLevels)},
      {PortText("Screen"), PortBenchScreenName(s.nShown, s.overlay)},
      {PortText("Frames shown"), s.nShown == 2 ? kDash
                                 : s.nFramesShown == 1 ? PortText("Held to the display")
                                                       : PortText("As many as it can")},
      {PortText("Renderer"), PictureName()},
  };
  for (const auto &row : rows) {
    y += 27.0f;
    Text(p, x, y, 12.6f, kLbInkDesc, row.first);
    const float fNameW = Width(p, 12.6f, row.first) + 10.0f;
    const float fValW = SDL_min(Width(p, 13.5f, row.second.c_str()), w - fNameW);
    TextIn(p, x + w - fValW, y, fValW + 0.5f, 13.5f, kLbInkInfo, row.second);
  }
  y += 22.0f;
  Text(p, x, y, 11.7f, kScInkOff, PortText("The set-up changes these between runs."));
  // the log
  y += 14.0f;
  Fill(p, {x, y, w, 1.8f}, kLbDivider);
  y += 9.0f + 13.5f;
  Title(p, x, y, PortText("LOG"));
  y += 18.0f;
  TextIn(p, x, y, w, 10.8f, kLbInkDesc, FolderShown());
  y += 8.0f;
  const char *const *ppTail = NULL;
  const int nTail = PortBenchTail(&ppTail);
  const float fTailH = 7.2f * 2.0f + 5 * 14.4f;
  const Rect tail = {x, y, w, fTailH};
  Fill(p, tail, UiRgb(0x000000, 0.35f), 2.7f);
  Ring(p, tail, kLbDivider, 2.7f, 0.9f);
  UiPushClip(tail.x * p.k, tail.y * p.k, (tail.x + tail.w) * p.k, (tail.y + tail.h) * p.k);
  float ty = y + 7.2f + 10.0f;
  Text(p, x + 7.2f, ty, 9.9f, kLbInkDesc, "second,shown,game,units,events,creep,ai,fog,\xe2\x80\xa6");
  if (nTail == 0)
    Text(p, x + 7.2f, ty + 14.4f, 9.9f, kScInkOff,
         RunState() == STATE_DONE ? "" : PortText("(the first line comes after a second)"));
  for (int i = 0; i < nTail; ++i)
    Text(p, x + 7.2f, ty + 14.4f * (i + 1), 9.9f, kLbInkInfo, ppTail[i]);
  UiPopClip();
}

void Minimap(const Pen &p, const Rect &m)
{
  if (m.w <= 0.0f)
    return;
  Fill(p, m, UiRgb(0x000000), 2.7f);
  if (g_bMinimap)
    UiPicture(m.x * p.k, m.y * p.k, m.w * p.k, m.h * p.k, kMinimapSlot, UiRgb(0xffffff), 2.7f * p.k);
  Fill(p, m, UiRgb(0x000000, 0.25f), 2.7f);
  const float d = SDL_max(2.4f, m.w / 110.0f);
  for (const Dot &dot : g_dots) {
    if (dot.u < 0.0f || dot.v < 0.0f || dot.u > 1.0f || dot.v > 1.0f)
      continue;
    const unsigned nRgb = dot.player < 32 ? kColours[dot.player] : 0xb0b0b0;
    const float r = dot.bBuilding ? d * 2.2f : d;  // a building larger, as the game's minimap
    UiFill((m.x + dot.u * m.w - r / 2.0f) * p.k, (m.y + dot.v * m.h - r / 2.0f) * p.k, r * p.k, r * p.k, UiRgb(nRgb));
  }
  // the camera's box
  if (g_nCamera != CAM_MAP && !NoPicture() && gwMapWidth > 0 && gwMapHeight > 0) {
    int x0, y0, x1, y1;
    PortViewScreenToMap(0, 0, &x0, &y0);
    PortViewScreenToMap(g_portView.nWidth, g_portView.nHeight, &x1, &y1);
    const float fW = gwMapWidth * 32.0f, fH = gwMapHeight * 32.0f;
    const float u0 = SDL_clamp(x0 / fW, 0.0f, 1.0f), v0 = SDL_clamp(y0 / fH, 0.0f, 1.0f);
    const float u1 = SDL_clamp(x1 / fW, 0.0f, 1.0f), v1 = SDL_clamp(y1 / fH, 0.0f, 1.0f);
    Ring(p, {m.x + u0 * m.w, m.y + v0 * m.h, (u1 - u0) * m.w, (v1 - v0) * m.h}, UiRgb(0xffffff), 0.0f, 1.1f);
  }
  AddHit(p, m, ACT_MINIMAP);
}

// A part's bar: its name, the track, its value and the other run's mark.
void PartRow(const Pen &p, float x, float y, float w, int i, double dMs, double dMost, double dVs, bool bVs)
{
  Text(p, x, y, 12.6f, kLbInkInfo, PortBenchPartName(i));
  const float bx = x + 63.0f, bw = w - 63.0f - 46.0f;
  Fill(p, {bx, y - 8.0f, bw, 8.1f}, UiRgb(0x3980bc, 0.14f), 1.8f);
  if (dMost > 0.0) {
    const float f = static_cast<float>(SDL_min(1.0, dMs / dMost));
    if (f > 0.0f)
      Fill(p, {bx, y - 8.0f, bw * f, 8.1f}, UiRgb(kParts[i]), 1.8f);
    if (bVs) {
      const float fv = static_cast<float>(SDL_min(1.0, dVs / dMost));
      Fill(p, {bx + bw * fv - 0.9f, y - 10.0f, 1.8f, 12.0f}, UiRgb(kVs));
    }
  }
  TextRight(p, x + w, y, 12.6f, kLbInkDesc, N2(dMs).c_str());
}

// Text broken into lines at spaces to fit w, at most nMost lines, the last cut with an ellipsis;
// the height it took.
float TextWrap(const Pen &p, float x, float y, float w, float fSize, UiColor ink, const std::string &text,
               int nMost = 3)
{
  const float fLine = fSize * 1.45f;
  std::string rest = text;
  int n = 0;
  while (!rest.empty() && n < nMost) {
    size_t cut = rest.size();
    if (Width(p, fSize, rest.c_str()) > w && n < nMost - 1) {
      cut = std::string::npos;
      for (size_t at = rest.rfind(' '); at != std::string::npos && at > 0; at = rest.rfind(' ', at - 1))
        if (Width(p, fSize, rest.substr(0, at).c_str()) <= w) {
          cut = at;
          break;
        }
      if (cut == std::string::npos)
        cut = rest.size();
    }
    TextIn(p, x, y + n * fLine, w, fSize, ink, rest.substr(0, cut));
    rest = cut < rest.size() ? rest.substr(cut + 1) : std::string();
    ++n;
  }
  return n * fLine;
}

void NumBox(const Pen &p, const Rect &r, const char *pszLabel, const std::string &value, const std::string &note,
            UiColor valueInk)
{
  Fill(p, r, kLbItem, 2.7f);
  Ring(p, r, kLbItemRim, 2.7f, 0.9f);
  const float x = r.x + 10.8f, w = r.w - 21.6f;
  // the label on two lines when it does not fit on one, broken at the last space that fits
  std::string first = pszLabel, second;
  if (Width(p, 11.7f, pszLabel) > w)
    for (size_t at = first.rfind(' '); at != std::string::npos && at > 0; at = first.rfind(' ', at - 1)) {
      if (Width(p, 11.7f, first.substr(0, at).c_str()) <= w) {
        second = first.substr(at + 1);
        first.resize(at);
        break;
      }
    }
  float y = r.y + 9.0f + 11.0f;
  TextIn(p, x, y, w, 11.7f, kLbInkDesc, first);
  if (!second.empty()) {
    y += 13.5f;
    TextIn(p, x, y, w, 11.7f, kLbInkDesc, second);
  }
  float fBig = 30.6f;
  while (fBig > 16.0f && Width(p, fBig, value.c_str(), UI_FACE_BOLD) > w)
    fBig -= 1.0f;
  TextIn(p, x, y + 31.0f, w, fBig, valueInk, value, UI_FACE_BOLD);
  TextIn(p, x, r.y + r.h - 10.0f, w, 11.7f, kLbInkDesc, note);
}

void RightPanel(const Pen &p, const Layout &l)
{
  const Rect &r = l.right;
  Panel(p, r);
  const float x = r.x + 14.4f, w = r.w - 28.8f;
  float y = r.y + 12.6f + 13.5f;
  Title(p, x, y, PortText("LIVE"));
  y += 9.0f;
  const PortBenchSecond *pSeconds = NULL;
  const int nSeconds = PortBenchHistory(&pSeconds);
  const PortBenchSecond *pLast = nSeconds > 0 ? &pSeconds[nSeconds - 1] : NULL;
  const PortBenchSettings &s = *PortBenchCurrent();
  const unsigned nFrame = PortBenchGameFrame();
  ReadAgainst();
  const bool bVs = HasAgainst();
  const int nVs = bVs ? AgainstSecond(nFrame) : -1;
  // the four numbers
  const float bw = (w - 7.2f) / 2.0f, bh = 92.0f;
  const double dFps = pLast ? pLast->dFramesPerS : 0.0, dGfps = pLast ? pLast->dGameFramesPerS : 0.0;
  const double dRate = SpeedRate();
  // {0} the speed it is held at
  std::string held = dRate <= 0.0 ? PortText("as fast as it goes")
                     : dGfps < dRate - 0.3 && pLast ? PortTextF("behind {0}", {SpeedLabel()})
                                                    : PortTextF("held at {0}", {SpeedLabel()});
  if (NoPicture())
    NumBox(p, {x, y, bw, bh}, PortText("Frames shown a second"), kDash, PortText("nothing shown"), UiRgb(kThis));
  else
    NumBox(p, {x, y, bw, bh}, PortText("Frames shown a second"), pLast ? N0(dFps) : kDash,
           pLast && dFps > 0.0 ? PortTextF("{0} ms a frame", {N2(1000.0 / dFps)}) : "", UiRgb(kThis));
  NumBox(p, {x + bw + 7.2f, y, bw, bh}, PortText("Game frames a second"), pLast ? N1(dGfps) : kDash, held,
         kLbInkInfo);
  y += bh + 7.2f;
  // {0} the players still in, {1} the players the run began with
  NumBox(p, {x, y, bw, bh}, PortText("Units on the map"), pLast ? N0(pLast->nUnits) : kDash,
         PortTextF("{0} of {1} players", {std::to_string(g_nPlayersLeft), std::to_string(s.nPlayers)}), kLbInkInfo);
  static double s_dWorst, s_dVsWorst;
  static int s_nWorstAt = -1;
  if (s_nWorstAt != nSeconds) {
    s_nWorstAt = nSeconds;
    s_dWorst = PortBenchWorstMs();
    s_dVsWorst = bVs ? AgainstWorst(nFrame) : 0.0;
  }
  std::string worstSmall = s_dWorst > 0.0 ? PortTextF("{0} ms", {N2(s_dWorst)}) : "";
  if (s_dVsWorst > 0.0)
    worstSmall += std::string(kDot) + g_vs.build + " " + N2(s_dVsWorst);
  NumBox(p, {x + bw + 7.2f, y, bw, bh}, PortText("Worst 1 % so far"), s_dWorst > 0.0 ? N0(1000.0 / s_dWorst) : kDash,
         worstSmall, kLbInkInfo);
  y += bh + 12.6f + 13.5f;
  Title(p, x, y, PortText("A FRAME, BY PART"));
  // the parts, ms a game frame in the last second, each against the other run's at the same game
  // frame
  double dMost = 0.01;
  for (int i = 0; i < PERF_PARTS; ++i) {
    if (pLast)
      dMost = SDL_max(dMost, pLast->adPartMs[i]);
    if (nVs >= 0)
      dMost = SDL_max(dMost, static_cast<double>(g_vs.secParts[nVs][i]));
  }
  y += 6.0f;
  double dSim = 0.0, dPic = 0.0;
  for (int i = 0; i < PERF_PARTS; ++i) {
    y += i == 8 ? 26.0f : 19.0f;
    if (i == 8)
      for (float d = 0.0f; d < w; d += 6.0f)
        Fill(p, {x + d, y - 19.0f, 3.0f, 0.9f}, kLbDivider);
    const double dMs = pLast ? pLast->adPartMs[i] : 0.0;
    (i < 8 ? dSim : dPic) += dMs;
    PartRow(p, x, y, w, i, dMs, dMost, nVs >= 0 ? g_vs.secParts[nVs][i] : 0.0, nVs >= 0);
  }
  y += 26.0f;
  float tx = x + Text(p, x, y, 12.6f, kLbInkInfo, N2(dSim).c_str(), UI_FACE_BOLD);
  Text(p, tx, y, 12.6f, kLbInkDesc, PortText(" ms the game frame"));
  y += 18.0f;
  tx = x + Text(p, x, y, 12.6f, kLbInkInfo, N2(dPic).c_str(), UI_FACE_BOLD);
  Text(p, tx, y, 12.6f, kLbInkDesc, PortText(" ms the picture"));
  // {0} the build the run is held against
  if (bVs)
    TextIn(p, x, y + 18.0f, w, 12.6f, kScInkOff,
           PortTextF("the mark: {0} at the same moment of the same game", {g_vs.build}));
}

// The graph's top: a round number over the most, with a tenth to spare.
float NiceTop(float fMost)
{
  const float kSteps[] = {2, 4, 5, 8, 10, 12, 16, 20, 25, 30, 40, 50, 80, 100, 150};
  for (float f : kSteps)
    if (fMost * 1.1f <= f)
      return f;
  return 150.0f;
}

void Foot(const Pen &p, const Layout &l)
{
  const Rect &r = l.foot;
  Panel(p, r);
  const float x = r.x + 12.6f, w = r.w - 25.2f;
  float hy = r.y + 9.0f + 15.0f;
  float hx = x + Text(p, x, hy + 4.5f, 13.5f, kLbInkInfo, PortText("FRAME TIME"), UI_FACE_WIDE) + 14.4f;
  const char *const kViews[] = {PortText("Frame time"), PortText("A game frame, by part"), PortText("Units")};
  for (int i = 0; i < 3; ++i)
    hx += Button(p, hx, hy - 13.5f, 27.0f, 11.7f, kViews[i], ACT_GRAPH, g_nGraph == i ? LOOK_PRESSED : LOOK_PLAIN, false,
                 0.0f, i) + 4.0f;
  hx += 10.0f;
  ReadAgainst();
  const bool bVs = HasAgainst();
  // the legend
  auto Key = [&](unsigned nRgb, bool bDashed, const std::string &label) {
    if (bDashed)
      for (float d = 0.0f; d < 18.0f; d += 6.0f)
        Fill(p, {hx + d, hy - 1.0f, 3.6f, 1.8f}, UiRgb(nRgb));
    else
      Fill(p, {hx, hy - 2.0f, 16.0f, 3.0f}, UiRgb(nRgb));
    hx += 22.0f;
    hx += Text(p, hx, hy + 4.0f, 11.7f, kLbInkDesc, label.c_str()) + 14.4f;
  };
  const Rect plot = {x, hy + 16.0f, w, r.y + r.h - 10.0f - (hy + 16.0f)};
  const float px0 = plot.x + 32.0f, px1 = plot.x + plot.w - 4.0f, py0 = plot.y + 7.0f, py1 = plot.y + plot.h - 16.0f;
  auto Y = [&](float v, float fTop) { return py1 - SDL_min(v, fTop) / fTop * (py1 - py0); };
  auto Axes = [&](float fTop, int nTicks, bool bUnits) {
    for (int t = 0; t <= nTicks; ++t) {
      const float v = fTop * t / nTicks, yy = Y(v, fTop);
      Fill(p, {px0, yy, px1 - px0, 0.9f}, UiRgb(0x3980bc, 0.22f));
      std::string label;
      if (bUnits && v >= 1000.0f)
        label = N1(v / 1000.0f) + "k";
      else
        label = N0(v);
      TextRight(p, px0 - 5.0f, yy + 3.5f, 9.9f, UiRgb(0x8dbfea, 0.85f), label.c_str());
    }
  };
  const PortBenchBucket *pB = NULL;
  const int nB = PortBenchBuckets(&pB);
  if (g_nGraph == GRAPH_TIME && NoPicture()) {
    Axes(10.0f, 5, false);
    const char *psz =
        PortText("No frames are shown, so there is no frame time; Units and A game frame, by part still draw.");
    Text(p, (px0 + px1) / 2.0f - Width(p, 11.7f, psz) / 2.0f, (py0 + py1) / 2.0f, 11.7f, kLbInkDesc, psz);
    return;
  }
  if (g_nGraph == GRAPH_TIME) {
    Key(kThis, false, PortText("this run, the last minute"));
    // {0} when the earlier run was made, {1} the build it ran on
    if (bVs)
      Key(kVs, true, PortTextF("{0}, build {1}, the same moment", {g_vs.when, g_vs.build}));
    const int nShow = 240, nFirst = SDL_max(0, nB - nShow);
    float fMost = 2.0f;
    std::vector<float> vs(static_cast<size_t>(nB - nFirst), 0.0f);
    for (int i = nFirst; i < nB; ++i) {
      fMost = SDL_max(fMost, pB[i].fMaxMs);
      if (bVs) {
        vs[i - nFirst] = AgainstMean(i > 0 ? pB[i - 1].nFrame : 0, pB[i].nFrame);
        fMost = SDL_max(fMost, vs[i - nFirst]);
      }
    }
    const float fTop = NiceTop(fMost);
    Axes(fTop, fTop <= 10.0f ? 3 : 5, false);
    // 60 and 144 a second
    const float afRate[2] = {1000.0f / 60.0f, 1000.0f / 144.0f};
    const char *const apszRate[2] = {PortText("60 a second"), PortText("144 a second")};
    for (int k = 0; k < 2; ++k)
      if (afRate[k] < fTop) {
        const float yy = Y(afRate[k], fTop);
        for (float d = px0; d < px1; d += 6.0f)
          Fill(p, {d, yy, 2.0f, 0.9f}, UiRgb(kGold, 0.35f));
        Text(p, px0 + 4.0f, yy - 4.0f, 9.9f, UiRgb(kGold, 0.7f), apszRate[k]);
      }
    if (nB - nFirst < 2) {
      const char *psz = RunState() == STATE_DONE ? PortText("Start the run and its frames are drawn here.")
                                                 : PortText("The first quarter second\xe2\x80\xa6");
      Text(p, (px0 + px1) / 2.0f - Width(p, 11.7f, psz) / 2.0f, (py0 + py1) / 2.0f, 11.7f, kLbInkDesc, psz);
      return;
    }
    const float bw = (px1 - px0) / nShow;
    for (int i = nFirst; i < nB; ++i) {
      const float bx = px0 + (i - nFirst) * bw;
      const float yMax = Y(pB[i].fMaxMs, fTop), yMean = Y(pB[i].fMeanMs, fTop);
      Fill(p, {bx, yMax, SDL_max(bw, 1.0f), SDL_max(yMean - yMax, 0.9f)}, UiRgb(kThis, 0.22f));
    }
    for (int i = nFirst + 1; i < nB; ++i) {
      const float bx0 = px0 + (i - 1 - nFirst + 0.5f) * bw, bx1 = px0 + (i - nFirst + 0.5f) * bw;
      if (bVs && vs[i - 1 - nFirst] > 0.0f && vs[i - nFirst] > 0.0f && (i % 2) == 0)
        Line(p, bx0, Y(vs[i - 1 - nFirst], fTop), bx1, Y(vs[i - nFirst], fTop), UiRgb(kVs, 0.9f), 1.2f);
      Line(p, bx0, Y(pB[i - 1].fMeanMs, fTop), bx1, Y(pB[i].fMeanMs, fTop), UiRgb(kThis), 1.6f);
    }
    return;
  }
  const PortBenchSecond *pS = NULL;
  const int nS = PortBenchHistory(&pS);
  if (g_nGraph == GRAPH_PARTS) {
    for (int i = 0; i < 8; ++i) {
      Fill(p, {hx, hy - 5.0f, 9.0f, 9.0f}, UiRgb(kParts[i]));
      hx += 13.0f;
      hx += Text(p, hx, hy + 4.0f, 11.7f, kLbInkDesc, PortBenchPartName(i)) + 10.0f;
    }
    const int nShow = 60, nFirst = SDL_max(0, nS - nShow);
    float fMost = 0.5f;
    for (int k = nFirst; k < nS; ++k) {
      double d = 0.0;
      for (int i = 0; i < 8; ++i)
        d += pS[k].adPartMs[i];
      fMost = SDL_max(fMost, static_cast<float>(d));
    }
    const float fTop = NiceTop(fMost);
    Axes(fTop, fTop <= 10.0f ? 3 : 5, false);
    if (nS - nFirst < 1)
      return;
    const float bw = (px1 - px0) / nShow;
    for (int k = nFirst; k < nS; ++k) {
      float fBelow = 0.0f;
      const float bx = px0 + (k - nFirst) * bw;
      for (int i = 0; i < 8; ++i) {
        const float v = static_cast<float>(pS[k].adPartMs[i]);
        const float yTop = Y(fBelow + v, fTop), yBot = Y(fBelow, fTop);
        if (yBot - yTop > 0.2f)
          Fill(p, {bx, yTop, SDL_max(bw - 1.0f, 1.0f), yBot - yTop}, UiRgb(kParts[i], 0.85f));
        fBelow += v;
      }
    }
    return;
  }
  // the units, over the whole run
  Key(kThis, false, PortText("units on the map, the whole run"));
  float fMost = 1.0f;
  for (int k = 0; k < nS; ++k)
    fMost = SDL_max(fMost, static_cast<float>(pS[k].nUnits));
  const float kSteps[] = {100, 250, 500, 1000, 2000, 2500, 4000, 5000, 8000, 10000, 20000};
  float fTop = 20000.0f;
  for (float f : kSteps)
    if (fMost * 1.1f <= f) {
      fTop = f;
      break;
    }
  Axes(fTop, 3, true);
  for (int k = 1; k < nS; ++k) {
    const float bx0 = px0 + (k - 1) * (px1 - px0) / SDL_max(1, nS - 1), bx1 = px0 + k * (px1 - px0) / SDL_max(1, nS - 1);
    Line(p, bx0, Y(static_cast<float>(pS[k - 1].nUnits), fTop), bx1, Y(static_cast<float>(pS[k].nUnits), fTop),
         UiRgb(kThis), 1.6f);
  }
}

// H: the overlay off, a counter at the game's top left.
void Counter(const Pen &p, const Layout &l)
{
  const PortBenchSecond *pS = NULL;
  const int nS = PortBenchHistory(&pS);
  std::string s =
      nS > 0 ? (NoPicture() ? std::string() : PortTextF("{0} fps", {N0(pS[nS - 1].dFramesPerS)}) + kDot) +
                   PortTextFill(PortTextC("game frames a second", "{0} game"),
                                {N1(pS[nS - 1].dGameFramesPerS)}) +
                   kDot + PortTextF("{0} units", {N0(pS[nS - 1].nUnits)})
             : std::string(PortText("the first second\xe2\x80\xa6"));
  const float x = g_nShown == SHOWN_WINDOW ? l.game.x + 9.0f : 16.0f;
  const float y = g_nShown == SHOWN_WINDOW ? l.game.y + 9.0f : 13.0f;
  const float w = Width(p, 12.6f, s.c_str()) + 18.0f;
  const Rect r = {x, y, w, 24.0f};
  Fill(p, r, UiRgb(0x000000, Hot(ACT_SHOW) ? 0.8f : 0.6f), 2.7f);
  Text(p, x + 9.0f, y + 16.5f, 12.6f, UiRgb(kThis), s.c_str());
  AddHit(p, r, ACT_SHOW);  // a click brings the overlay back, as H does
}

// ---------------------------------------------------------------------------
// The overlay's views (PRD 2026-10-17; bench.js's ovRects, buildReadout, buildStrip, selView,
// buildPlayers): what shows and where comes from the live overlay (port_benchoverlay.cpp), which
// O changes. Sizes are the design's at 1200 high, times 0.9 for this stage of 1080.

const PortBenchOverlay &Ov(void)
{
  return PortBenchOverlayLive();
}

bool Shows(unsigned nBits)
{
  return (PortBenchOverlayShows(Ov()) & nBits) != 0;
}

// the panels' fill: rgba(3,12,24) at 0.04 + 0.92 x (1 - see)
float SeeAlpha(void)
{
  const float fSee = PortBenchOverlaySee(Ov(), PortBenchCurrent()->nShown) / 100.0f;
  return 0.04f + 0.92f * (1.0f - fSee);
}

// A toast, centred under the controls (O's).
std::string g_toast;
Uint64 g_nToastUntil;
Uint64 g_nMovedAt;  // the mouse's last move, for the controls that show on a move

void Toast(const std::string &s)
{
  g_toast = s;
  g_nToastUntil = SDL_GetTicks() + 1800;
}

const char *const kRaces[3] = {PT_("Zerg"), PT_("Terran"), PT_("Protoss")};
const char kRaceLetters[3] = {'Z', 'T', 'P'};

// The players as the reader gives them, once a drawn frame.
PortBenchPlayer g_players[PLAYER_SLOTS_MAX];
int g_nPlayers;

const PortBenchPlayer *PlayerOf(int nSlot)
{
  for (int i = 0; i < g_nPlayers; ++i)
    if (g_players[i].nSlot == nSlot)
      return &g_players[i];
  return NULL;
}

std::string OwnerLine(int nSlot, bool bTeam)
{
  const PortBenchPlayer *pl = PlayerOf(nSlot);
  if (nSlot == 11)
    return PortText("Neutral");
  // {0} the slot the computer plays in
  std::string s = PortTextF("Computer {0}", {std::to_string(nSlot + 1)});
  if (pl != NULL && pl->nRace >= 0 && pl->nRace <= 2)
    s += std::string(kDot) + PortText(kRaces[pl->nRace]);
  s += std::string(kDot) + PortText(AiLevelName(AiProfileOf(nSlot).bLevel));
  if (bTeam && pl != NULL && PortBenchCurrent()->szTeams[0] != '\0')
    s += std::string(kDot) + PortTextF("Team {0}", {std::to_string(pl->nTeam)});
  if (pl != NULL && !pl->bLeft)
    s += std::string(kDot) + PortText("out");
  return s;
}

// a unit's name without its race, as "Training Zealot" has it
std::string ShortName(const char *psz)
{
  for (const char *pszRace : {"Terran ", "Zerg ", "Protoss "})
    if (strncmp(psz, pszRace, strlen(pszRace)) == 0)
      return psz + strlen(pszRace);
  return psz;
}

// hit points' colour by the share left
unsigned HpRgb(float f)
{
  return f > 0.66f ? 0x3cd33c : f > 0.33f ? 0xe0c828 : 0xd83a2a;
}

// The wireframes: a sheet of 6 by 6 cells of 64 pixels in one picture slot, a cell a unit type,
// decoded from the game's wireframes as they are first wanted (PortWireframeCell).
const int kWireSlot = 15, kWireCell = 64, kWireAcross = 6;
std::vector<unsigned char> g_wireSheet;
int g_anWireType[kWireAcross * kWireAcross];
int g_nWires;

int WireCellOf(int nType)
{
  for (int i = 0; i < g_nWires; ++i)
    if (g_anWireType[i] == nType)
      return i;
  const int nSide = kWireCell * kWireAcross;
  if (g_wireSheet.empty() || g_nWires == kWireAcross * kWireAcross) {
    g_wireSheet.assign(static_cast<size_t>(nSide) * nSide * 4, 0);
    g_nWires = 0;
  }
  const int nCell = g_nWires;
  unsigned char *pCell =
      &g_wireSheet[(static_cast<size_t>(nCell / kWireAcross) * kWireCell * nSide + (nCell % kWireAcross) * kWireCell) * 4];
  if (!PortWireframeCell(nType, pCell, nSide, kWireCell))
    return -1;
  g_anWireType[g_nWires++] = nType;
  UiPictureSet(kWireSlot, g_wireSheet.data(), nSide, nSide);
  return nCell;
}

// a wireframe in r, tinted c; nothing when the game has none for it
void Wire(const Pen &p, const Rect &r, int nType, UiColor c)
{
  const int nCell = WireCellOf(nType);
  if (nCell < 0)
    return;
  const float fSide = SDL_min(r.w, r.h);
  const float u0 = static_cast<float>((nCell % kWireAcross) * kWireCell), v0 = static_cast<float>((nCell / kWireAcross) * kWireCell);
  UiPicturePart((r.x + (r.w - fSide) / 2.0f) * p.k, (r.y + (r.h - fSide) / 2.0f) * p.k, fSide * p.k, fSide * p.k,
                kWireSlot, u0, v0, u0 + kWireCell, v0 + kWireCell, c, true);
}

// the portrait's place: the design's stand-in, stripes and the name, a click to the unit
void Portrait(const Pen &p, const Rect &r, const char *pszName, int nIndex)
{
  Fill(p, r, UiRgb(0x000000), 2.7f);
  UiPushClip(r.x * p.k, r.y * p.k, (r.x + r.w) * p.k, (r.y + r.h) * p.k);
  for (float d = -r.h; d < r.w; d += 14.0f)
    Line(p, r.x + d, r.y + r.h, r.x + d + r.h, r.y, UiRgb(0x3980bc, 0.10f), 5.0f);
  UiPopClip();
  Ring(p, r, Hot(ACT_PORTRAIT, nIndex) ? kLbHyperionRim : kLbInnerRim, 2.7f, 1.8f);
  const std::string name = ShortName(pszName);
  TextIn(p, r.x + (r.w - SDL_min(Width(p, 12.6f, name.c_str()), r.w - 12.0f)) / 2.0f, r.y + r.h / 2.0f + 4.0f,
         r.w - 12.0f, 12.6f, kLbInkDesc, name);
  AddHit(p, r, ACT_PORTRAIT, nIndex);
}

// a bar: its label, the track and its fill, "v / max"
void Bar(const Pen &p, float x, float y, float fLabelW, float w, float fValW, const char *pszLabel, int v, int nMax,
         unsigned nRgb, float fSize)
{
  Text(p, x, y + fSize * 0.35f, fSize * 0.96f, kLbInkDesc, pszLabel);
  const std::string val = std::to_string(v) + " / " + std::to_string(nMax);
  const float fRoom = SDL_max(fValW, Width(p, fSize, val.c_str()));
  const Rect track = {x + fLabelW, y - 4.0f, w - fLabelW - fRoom - 8.0f, 8.1f};
  Fill(p, track, UiRgb(0x3980bc, 0.18f), 2.0f);
  if (nMax > 0 && v > 0)
    Fill(p, {track.x, track.y, track.w * SDL_min(1.0f, static_cast<float>(v) / nMax), track.h}, UiRgb(nRgb), 2.0f);
  TextRight(p, x + w, y + fSize * 0.35f, fSize, kLbInkInfo, val.c_str());
}

// The selection: one unit's figures, a building's work, a transport's load; or up to 12 in a
// grid. bCard is the readout's and the strip's small card: no wireframe, no queue.
void OneUnit(const Pen &p, const Rect &r, const PortBenchUnit &u, bool bCard)
{
  const float fWireW = bCard ? 0.0f : 118.8f, fPortW = bCard ? 109.8f : 151.2f, fGap = 12.6f;
  const float fPad = bCard ? 9.0f : 12.6f;
  float x = r.x + fPad;
  const float y0 = r.y + fPad;
  const float fHpF = u.nHpMax > 0 ? static_cast<float>(u.nHp) / u.nHpMax : 1.0f;
  if (!bCard) {
    const Rect wr = {x, y0, fWireW, fWireW};
    Fill(p, wr, UiRgb(0x000000, 0.45f), 2.0f);
    Ring(p, wr, kLbItemRim, 2.0f, 0.9f);
    Wire(p, {wr.x + 6.0f, wr.y + 6.0f, wr.w - 12.0f, wr.h - 12.0f}, u.nType, UiRgb(HpRgb(fHpF)));
    x += fWireW + fGap;
  }
  const float fInfoW = r.x + r.w - fPad - fPortW - fGap - x;
  const float fPortH = SDL_min(r.h - 2.0f * fPad, bCard ? 120.0f : 185.4f);
  Portrait(p, {r.x + r.w - fPad - fPortW, y0, fPortW, fPortH}, u.szName, u.nIndex);
  // the name, its kind, the owner
  float y = y0 + (bCard ? 15.0f : 17.0f);
  const float fNameSize = bCard ? 16.2f : 18.0f;
  const std::string name = ShortName(u.szName);
  const float fNameW = Text(p, x, y, fNameSize, kScInkHead, name.c_str(), UI_FACE_BOLD);
  const char *pszKind = u.nResources >= 0 ? PortText("Resource")
                        : u.bBuilding     ? PortText("Structure")
                        : u.bWorker       ? PortText("Worker")
                        : u.bAir          ? PortText("Air unit")
                                          : PortText("Ground unit");
  Text(p, x + fNameW + 9.0f, y, 11.7f, kLbInkDesc, pszKind);
  y += bCard ? 18.0f : 21.0f;
  Fill(p, {x, y - 8.0f, 9.0f, 9.0f}, UiRgb(u.nPlayer < 32 ? kColours[u.nPlayer] : 0xb0b0b0), 1.0f);
  TextIn(p, x + 14.0f, y, fInfoW - 14.0f, 12.6f, kLbInkInfo, OwnerLine(u.nPlayer, !bCard));
  // the bars
  const float fSize = bCard ? 11.7f : 12.2f, fLabelW = bCard ? 57.6f : 70.2f, fValW = bCard ? 75.6f : 82.8f;
  y += bCard ? 17.0f : 20.0f;
  if (u.nResources >= 0) {
    const float fW = Text(p, x, y + fSize * 0.35f, fSize * 0.96f, kLbInkDesc, PortText("Resources left"));
    Text(p, x + fW + 10.0f, y + fSize * 0.35f, fSize, kLbInkInfo, N0(u.nResources).c_str(), UI_FACE_BOLD);
    return;
  }
  Bar(p, x, y, fLabelW, fInfoW, fValW, PortText("Hit points"), u.nHp, u.nHpMax, HpRgb(fHpF), fSize);
  if (u.nShieldsMax > 0) {
    y += bCard ? 15.0f : 17.0f;
    Bar(p, x, y, fLabelW, fInfoW, fValW, PortText("Shields"), u.nShields, u.nShieldsMax, 0x5fb6ff, fSize);
  }
  if (u.nEnergyMax > 0) {
    y += bCard ? 15.0f : 17.0f;
    Bar(p, x, y, fLabelW, fInfoW, fValW, PortText("Energy"), u.nEnergy, u.nEnergyMax, 0xc792ea, fSize);
  }
  // the chips: kills, armour, the weapon
  y += bCard ? 19.0f : 22.0f;
  std::string chips;
  // {0} how many the unit has killed, {1} its armour, with what an upgrade adds
  if (!u.bBuilding && u.bHasWeapon)
    chips += PortTextF("Kills {0}", {std::to_string(u.nKills)}) + "    ";
  chips += PortTextF("Armour {0}", {std::to_string(u.nArmour) +
                                    (u.nArmourUp > 0 ? " +" + std::to_string(u.nArmourUp) : std::string())});
  if (u.bHasWeapon && u.szWeapon[0] != '\0')
    chips += "    " + std::string(u.szWeapon) + " " + std::to_string(u.nDamage) +
             (u.nDamageUp > 0 ? " +" + std::to_string(u.nDamageUp) : "");
  TextIn(p, x, y, fInfoW, 12.2f, kLbInkInfo, chips);
  // a building's work, a transport's load
  if (u.nQueue > 0 || u.nProgressPercent >= 0 || u.nLarvae > 0) {
    y += bCard ? 18.0f : 22.0f;
    const PortBenchPlayer *pl = PlayerOf(u.nPlayer);
    std::string work;
    if (u.nQueue > 0 && u.nProgressPercent >= 0) {
      char szName[64] = "";
      if (u.anQueue[0] >= 0 && u.anQueue[0] < 228)
        lstrcpynA(szName, get_unit_name_from_type(static_cast<WORD>(u.anQueue[0])), sizeof szName);
      // {0} what is being made
      work = (pl != NULL && pl->nRace == 0 ? PortTextF("Morphing {0}", {ShortName(szName)})
                                           : PortTextF("Training {0}", {ShortName(szName)})) +
             kDot + std::to_string(u.nProgressPercent) + " %";
    } else if (u.nProgressPercent >= 0) {
      work = std::string(u.bBuilding ? PortText("Going up") : PortText("Morphing")) + kDot +
             std::to_string(u.nProgressPercent) + " %";
    }
    if (u.nLarvae > 0)
      work += (work.empty() ? "" : std::string(kDot)) +
              PortTextNF("{0} larva", "{0} larvae", u.nLarvae, {std::to_string(u.nLarvae)});
    TextIn(p, x, y, fInfoW, 12.2f, kLbInkInfo, work);
    if (!bCard && u.nQueue > 0) {
      y += 8.0f;
      for (int i = 0; i < 5; ++i) {
        const Rect q = {x + i * 40.0f, y, 36.0f, 32.4f};
        Fill(p, q, UiRgb(0x000000, 0.4f), 2.0f);
        Ring(p, q, i == 0 && u.nQueue > 0 ? kLbHyperionRim : kLbItemRim, 2.0f, 0.9f);
        if (i < u.nQueue && u.anQueue[i] >= 0 && u.anQueue[i] < 228)
          Wire(p, {q.x + 3.0f, q.y + 2.0f, q.w - 6.0f, q.h - 4.0f}, u.anQueue[i], UiRgb(0x3cd33c));
      }
      y += 32.4f;
    }
    if (u.nProgressPercent >= 0) {
      y += 7.0f;
      const Rect bar = {x, y, SDL_min(198.0f, fInfoW), 4.5f};
      Fill(p, bar, UiRgb(0x3980bc, 0.18f), 2.0f);
      Fill(p, {bar.x, bar.y, bar.w * u.nProgressPercent / 100.0f, bar.h}, kLbHyperionRim, 2.0f);
    }
  } else if (u.bTransport) {
    y += bCard ? 18.0f : 22.0f;
    // {0} the spaces taken, {1} the spaces it holds
    TextIn(p, x, y, fInfoW, 12.2f, kLbInkInfo,
           PortText("Carrying") + std::string(kDot) +
               PortTextF("{0} of {1} spaces",
                         {std::to_string(u.nCargoUsed), std::to_string(u.nCargoHold)}));
    y += 8.0f;
    const float fSlotW = bCard ? 27.0f : 36.0f, fSlotH = bCard ? 23.4f : 32.4f;
    float sx = x;
    for (int i = 0; i < u.nCargo && sx < x + fInfoW; ++i) {
      const int nWide = SDL_max(1, u.anCargoSpace[i]);
      const Rect q = {sx, y, fSlotW * nWide + 4.0f * (nWide - 1), fSlotH};
      Fill(p, q, UiRgb(0x000000, 0.4f), 2.0f);
      Ring(p, q, kLbItemRim, 2.0f, 0.9f);
      Wire(p, {q.x + 2.0f, q.y + 2.0f, q.w - 4.0f, q.h - 4.0f}, u.anCargo[i], UiRgb(0x3cd33c));
      sx += q.w + 4.0f;
    }
  }
}

void ManyUnits(const Pen &p, const Rect &r, const PortBenchUnit *pu, int n, bool bCard)
{
  const float fPad = bCard ? 9.0f : 12.6f;
  const float x = r.x + fPad, w = r.w - 2.0f * fPad;
  float y = r.y + fPad + 15.0f;
  // {0} how many units are selected
  const float fHeadW =
      Text(p, x, y, 15.3f, kScInkHead, PortTextF("{0} selected", {std::to_string(n)}).c_str(), UI_FACE_BOLD);
  // the owners: one's line, or each with its count
  int anCount[PLAYER_SLOTS_MAX] = {};
  int nOwners = 0, nOwner = -1;
  for (int i = 0; i < n; ++i)
    if (pu[i].nPlayer < PLAYER_SLOTS_MAX && anCount[pu[i].nPlayer]++ == 0) {
      ++nOwners;
      nOwner = pu[i].nPlayer;
    }
  std::string owners;
  if (nOwners == 1) {
    owners = OwnerLine(nOwner, !bCard);
  } else {
    for (int s = 0; s < PLAYER_SLOTS_MAX; ++s)
      if (anCount[s] > 0)
        owners += (owners.empty() ? "" : std::string(kDot)) +
                  PortTextF("Computer {0}", {std::to_string(s + 1)}) + " " + std::to_string(anCount[s]);
  }
  TextIn(p, x + fHeadW + 12.0f, y, w - fHeadW - 12.0f - (bCard ? 0.0f : 190.0f), 12.6f, kLbInkInfo, owners);
  if (!bCard)
    TextRight(p, x + w, y, 10.8f, kScInkOff, PortText("A click on one shows it on the map"));
  y += 10.0f;
  // the grid, 6 by 2
  const float fGap = bCard ? 3.6f : 5.4f;
  const float cw = (w - 5.0f * fGap) / 6.0f;
  const float ch = bCard ? 43.2f : SDL_min(cw, (r.y + r.h - fPad - y - fGap) / 2.0f);
  const int nCells = bCard ? n : 12;
  for (int i = 0; i < nCells; ++i) {
    const Rect c = {x + (i % 6) * (cw + fGap), y + (i / 6) * (ch + fGap), cw, ch};
    if (i >= n) {
      for (float d = 0.0f; d < c.w; d += 6.0f) {
        Fill(p, {c.x + d, c.y, 3.0f, 0.9f}, kLbDivider);
        Fill(p, {c.x + d, c.y + c.h - 0.9f, 3.0f, 0.9f}, kLbDivider);
      }
      continue;
    }
    const PortBenchUnit &u = pu[i];
    const float f = u.nHpMax > 0 ? static_cast<float>(u.nHp) / u.nHpMax : 1.0f;
    Fill(p, c, UiRgb(0x000000, 0.45f), 2.0f);
    Ring(p, c, Hot(ACT_PORTRAIT, u.nIndex) ? kLbHyperionRim : kLbItemRim, 2.0f, 0.9f);
    Fill(p, {c.x, c.y, 2.7f, c.h}, UiRgb(u.nPlayer < 32 ? kColours[u.nPlayer] : 0xb0b0b0));
    Wire(p, {c.x + 5.0f, c.y + 2.0f, c.w - 8.0f, c.h - 10.0f}, u.nType, UiRgb(HpRgb(f)));
    const Rect hp = {c.x + 6.6f, c.y + c.h - 6.3f, c.w - 10.2f, 3.6f};
    Fill(p, hp, UiRgb(0x000000, 0.6f));
    Fill(p, {hp.x, hp.y, hp.w * SDL_min(1.0f, f), hp.h}, UiRgb(HpRgb(f)));
    AddHit(p, c, ACT_PORTRAIT, u.nIndex);
  }
}

PortBenchUnit g_sel[PORT_BENCH_SELECT_MAX];
int g_nSel;

void ReadSelection(void)
{
  g_nSel = NoPicture() ? 0 : PortBenchSelected(g_sel, PORT_BENCH_SELECT_MAX);
  g_nPlayers = PortBenchPlayersRead(g_players, PLAYER_SLOTS_MAX);
}

// the panel's selection (Full overlay, or under the window)
void SelectionPanel(const Pen &p, const Rect &r)
{
  Panel(p, r);
  if (g_nSel == 0) {
    Title(p, r.x + 14.4f, r.y + 26.0f, PortText("SELECTION"));
    TextWrap(p, r.x + 14.4f, r.y + 52.0f, SDL_min(r.w - 28.8f, 468.0f), 13.5f, kLbInkDesc,
             NoPicture() ? PortText("No picture, so nothing to select.")
                         : PortText("Nothing selected. Click a unit or drag a box round them, as in the game; Shift adds or "
                                    "takes out. Right-drag moves the camera."),
             4);
    return;
  }
  if (g_nSel == 1)
    OneUnit(p, r, g_sel[0], false);
  else
    ManyUnits(p, r, g_sel, g_nSel, false);
}

// the readout's and the strip's card, only while something is selected; its foot fixed
void SelectionCard(const Pen &p, float x, float yFoot)
{
  if (g_nSel == 0 || !Shows(PORT_OV_SELECTION))
    return;
  // as tall as OneUnit's lines, the portrait's 120 at the least
  float h = 0.0f;
  if (g_nSel > 1) {
    h = 9.0f + 25.0f + ((g_nSel + 5) / 6) * (43.2f + 3.6f) + 9.0f;
  } else {
    const PortBenchUnit &u = g_sel[0];
    h = 9.0f + 15.0f + 18.0f + 17.0f + (u.nShieldsMax > 0 ? 15.0f : 0.0f) + (u.nEnergyMax > 0 ? 15.0f : 0.0f) + 19.0f;
    if (u.nQueue > 0 || u.nProgressPercent >= 0 || u.nLarvae > 0)
      h += 18.0f + (u.nProgressPercent >= 0 ? 11.5f : 0.0f);
    else if (u.bTransport)
      h += 18.0f + 8.0f + 23.4f;
    h = SDL_max(h + 14.0f, 138.0f);
  }
  const Rect r = {x, yFoot - h, 450.0f, h};
  Panel(p, r);
  if (g_nSel == 1)
    OneUnit(p, r, g_sel[0], true);
  else
    ManyUnits(p, r, g_sel, g_nSel, true);
}

// The players: a row each, by slot or under their teams; one out struck through.
void PlayersList(const Pen &p, const Rect &r, float y, bool bNarrow)
{
  const float x = r.x + 14.4f, w = r.w - 28.8f;
  const bool bTeams = PortBenchCurrent()->szTeams[0] != '\0';
  int nIn = 0, nTeamsIn = 0;
  bool abTeamIn[PLAYER_SLOTS_MAX + 1] = {};
  for (int i = 0; i < g_nPlayers; ++i)
    if (g_players[i].bLeft) {
      ++nIn;
      if (g_players[i].nTeam >= 0 && g_players[i].nTeam <= PLAYER_SLOTS_MAX && !abTeamIn[g_players[i].nTeam]) {
        abTeamIn[g_players[i].nTeam] = true;
        ++nTeamsIn;
      }
    }
  Title(p, x, y, PortText("PLAYERS"));
  int nTeams = 0;
  bool abTeam[PLAYER_SLOTS_MAX + 1] = {};
  for (int i = 0; i < g_nPlayers; ++i)
    if (g_players[i].nTeam >= 0 && g_players[i].nTeam <= PLAYER_SLOTS_MAX && !abTeam[g_players[i].nTeam]) {
      abTeam[g_players[i].nTeam] = true;
      ++nTeams;
    }
  // {0} the teams or players still in, {1} how many there were
  const std::string left =
      bTeams ? PortTextF("{0} of {1} teams left", {std::to_string(nTeamsIn), std::to_string(nTeams)})
             : PortTextF("{0} of {1} left", {std::to_string(nIn), std::to_string(g_nPlayers)});
  TextRight(p, x + w, y, 11.7f, kLbInkDesc, left.c_str());
  // the columns: number, level (and race), minerals, gas, supply, units
  const float cNum = 23.4f, cMin = 48.6f, cGas = bNarrow ? 0.0f : 41.4f, cSup = 50.4f, cUnits = 46.8f;
  const float xUnits = x + w, xSup = xUnits - cUnits, xGas = xSup - cSup, xMin = xGas - cGas;
  y += 18.0f;
  Text(p, x + cNum, y, 9.9f, kLbInkDesc, bNarrow ? "" : PortText("LEVEL"));
  TextRight(p, xMin, y, 9.9f, kLbInkDesc, PortText("MINERALS"));
  if (!bNarrow)
    TextRight(p, xGas, y, 9.9f, kLbInkDesc, PortText("GAS"));
  TextRight(p, xSup, y, 9.9f, kLbInkDesc, PortText("SUPPLY"));
  TextRight(p, xUnits, y, 9.9f, kLbInkDesc, PortText("UNITS"));
  const float fRowH = 18.9f;
  auto Row = [&](const PortBenchPlayer &pl) {
    if (y + fRowH > r.y + r.h - 8.0f)
      return;
    y += fRowH;
    const Rect row = {x - 4.0f, y - 13.5f, w + 8.0f, fRowH};
    if (Hot(ACT_PLAYER, pl.nSlot))
      Fill(p, row, kLbItemHi, 2.0f);
    AddHit(p, row, ACT_PLAYER, pl.nSlot);
    const bool bOut = !pl.bLeft;
    const UiColor ink = bOut ? kScInkOff : kLbInkInfo;
    const Rect chip = {x, y - 11.0f, 19.8f, 14.4f};
    Fill(p, chip, UiRgb(pl.nSlot < 32 ? kColours[pl.nSlot] : 0xb0b0b0, bOut ? 0.45f : 1.0f), 2.0f);
    const std::string num = std::to_string(pl.nSlot + 1);
    Text(p, chip.x + (chip.w - Width(p, 9.9f, num.c_str(), UI_FACE_BOLD)) / 2.0f, y, 9.9f, UiRgb(0x000000),
         num.c_str(), UI_FACE_BOLD);
    float lx = x + cNum;
    if (pl.nRace >= 0 && pl.nRace <= 2) {
      const char sz[2] = {kRaceLetters[pl.nRace], 0};
      lx += Text(p, lx, y, 9.0f, kLbInkDesc, sz, UI_FACE_WIDE) + 5.0f;
    }
    if (!bNarrow) {
      const std::string level = PortText(AiLevelName(AiProfileOf(pl.nSlot).bLevel));
      const float fLevelW = TextIn(p, lx, y, xMin - cMin - lx - 6.0f, 11.7f, ink, level);
      if (bOut)
        Fill(p, {lx, y - 4.0f, fLevelW, 0.9f}, ink);
    }
    TextRight(p, xMin, y, 11.2f, ink, N0(pl.nMineralsGathered).c_str());
    if (!bNarrow)
      TextRight(p, xGas, y, 11.2f, ink, N0(pl.nGasGathered).c_str());
    TextRight(p, xSup, y, 11.2f, ink,
              bOut ? kDash : (std::to_string(pl.nSupplyUsed) + "/" + std::to_string(pl.nSupplyMax)).c_str());
    // {0} the game time the player went out at
    TextRight(p, xUnits, y, bOut ? 9.9f : 11.2f, ink,
              bOut ? PortTextF("out {0}", {GameClock(pl.nOutFrame > PortBenchGameFrame() ? 0 : pl.nOutFrame)}).c_str()
                   : std::to_string(pl.nUnits).c_str());
  };
  if (!bTeams) {
    for (int i = 0; i < g_nPlayers; ++i)
      Row(g_players[i]);
    return;
  }
  for (int t = 0; t <= PLAYER_SLOTS_MAX; ++t) {
    if (!abTeam[t])
      continue;
    int nMembers = 0, nMembersIn = 0;
    for (int i = 0; i < g_nPlayers; ++i)
      if (g_players[i].nTeam == t) {
        ++nMembers;
        nMembersIn += g_players[i].bLeft;
      }
    if (y + 22.0f > r.y + r.h - 8.0f)
      return;
    y += 22.0f;
    Fill(p, {x, y - 15.0f, w, 0.9f}, kLbDivider);
    Fill(p, {x, y - 8.0f, 9.0f, 9.0f}, UiRgb(kColours[(t + 15) % 32]), 1.0f);
    Text(p, x + 14.0f, y, 11.7f, kLbInkInfo, PortTextF("Team {0}", {std::to_string(t)}).c_str(), UI_FACE_BOLD);
    // {0} the team's players still in, {1} how many it has
    TextRight(p, x + w, y, 10.8f, kLbInkDesc,
              nMembersIn == 0
                  ? PortText("out")
                  : PortTextF("{0} of {1} in", {std::to_string(nMembersIn), std::to_string(nMembers)}).c_str());
    for (int i = 0; i < g_nPlayers; ++i)
      if (g_players[i].nTeam == t)
        Row(g_players[i]);
  }
}

// the base of a player: its first building, else its first unit
bool PlayerBase(int nSlot, int *px, int *py)
{
  CUnit *pAny = NULL;
  for (CUnit *pUnit = CUnit::GetUsedList(); pUnit != NULL; pUnit = pUnit->pNext) {
    if (pUnit->bPlayer != nSlot)
      continue;
    if (pUnit->dwFlags & 2) {
      *px = pUnit->wPositionX;
      *py = pUnit->wPositionY;
      return true;
    }
    if (pAny == NULL)
      pAny = pUnit;
  }
  if (pAny == NULL)
    return false;
  *px = pAny->wPositionX;
  *py = pAny->wPositionY;
  return true;
}

// ---------------------------------------------------------------------------
// A readout in a corner of the picture (small or Large readings) and a strip along an edge.

struct Figures {
  double dFps, dLow, dMs, dWorstMs, dGfps, dVs;
  unsigned nUnits;
  bool bHave, bVs;
  std::string held, vsNote;
};

Figures FiguresNow(void)
{
  Figures f = {};
  const PortBenchSecond *pS = NULL;
  const int nS = PortBenchHistory(&pS);
  f.bHave = nS > 0;
  if (f.bHave) {
    f.dFps = pS[nS - 1].dFramesPerS;
    f.dGfps = pS[nS - 1].dGameFramesPerS;
    f.nUnits = pS[nS - 1].nUnits;
    f.dMs = f.dFps > 0.0 ? 1000.0 / f.dFps : 0.0;
  }
  f.dWorstMs = PortBenchWorstMs();
  f.dLow = f.dWorstMs > 0.0 ? 1000.0 / f.dWorstMs : 0.0;
  const double dRate = SpeedRate();
  f.held = dRate <= 0.0                              ? PortText("as fast as it goes")
           : f.bHave && f.dGfps < dRate - 0.3 ? PortTextF("behind {0}", {SpeedLabel()})
                                                      : PortTextF("held at {0}", {SpeedLabel()});
  ReadAgainst();
  f.bVs = HasAgainst();
  if (f.bVs) {
    const int nVs = AgainstSecond(PortBenchGameFrame());
    const float fVsMs = nVs >= 0 && nVs < static_cast<int>(g_vs.secMs.size()) ? g_vs.secMs[nVs] : 0.0f;
    if (fVsMs > 0.0f && f.dFps > 0.0)
      f.dVs = (f.dFps / (1000.0 / fVsMs) - 1.0) * 100.0;
    else
      f.bVs = false;
  }
  // {0} the build the run is held against
  f.vsNote = HasAgainst() ? PortTextF("on {0}", {g_vs.build}) : PortText("no earlier run");
  return f;
}

// the frames' health: against the held run, else against 144 (60 when held to the display)
unsigned HealthRgb(const Figures &f)
{
  if (f.bVs)
    return f.dVs >= -2.0 ? kGood : f.dVs >= -10.0 ? kGold : kBad;
  const double dTarget = PortBenchCurrent()->nFramesShown == 1 ? 60.0 : 144.0;
  return f.dFps >= 0.97 * dTarget ? kGood : f.dFps >= SDL_min(60.0, 0.6 * dTarget) ? kGold : kBad;
}

std::string VsText(const Figures &f)
{
  if (!f.bVs)
    return kDash;
  char sz[32];
  snprintf(sz, sizeof sz, "%s%.1f %%", f.dVs >= 0.0 ? "+" : "\xe2\x88\x92", f.dVs >= 0.0 ? f.dVs : -f.dVs);
  return sz;
}

// the last minute of shown frames, a quarter second at a time: the worst as a band, the mean as
// a line, the held run dashed, and 60 and 144 a second dotted
void Spark(const Pen &p, const Rect &r, float fScale)
{
  Fill(p, r, UiRgb(0x000000, 0.3f), 2.0f);
  const PortBenchBucket *pB = NULL;
  const int nB = PortBenchBuckets(&pB);
  const int nShow = 240, nFirst = SDL_max(0, nB - nShow);
  ReadAgainst();
  const bool bVs = HasAgainst();
  float fMost = 2.0f;
  for (int i = nFirst; i < nB; ++i)
    fMost = SDL_max(fMost, pB[i].fMaxMs);
  const float fTop = NiceTop(fMost);
  auto Y = [&](float v) { return r.y + r.h - 2.0f - SDL_min(v, fTop) / fTop * (r.h - 4.0f); };
  for (float fRate : {1000.0f / 60.0f, 1000.0f / 144.0f})
    if (fRate < fTop)
      for (float d = r.x; d < r.x + r.w; d += 6.0f)
        Fill(p, {d, Y(fRate), 2.0f, 0.9f}, UiRgb(kGold, 0.35f));
  if (nB - nFirst < 2)
    return;
  const float bw = r.w / nShow;
  for (int i = nFirst; i < nB; ++i) {
    const float bx = r.x + (i - nFirst) * bw;
    const float yMax = Y(pB[i].fMaxMs), yMean = Y(pB[i].fMeanMs);
    Fill(p, {bx, yMax, SDL_max(bw, 0.9f), SDL_max(yMean - yMax, 0.9f)}, UiRgb(0x66d5fe, 0.25f));
  }
  for (int i = nFirst + 1; i < nB; ++i) {
    const float bx0 = r.x + (i - 1 - nFirst + 0.5f) * bw, bx1 = r.x + (i - nFirst + 0.5f) * bw;
    if (bVs && (i % 2) == 0) {
      const float v0 = AgainstMean(i > 1 ? pB[i - 2].nFrame : 0, pB[i - 1].nFrame);
      const float v1 = AgainstMean(pB[i - 1].nFrame, pB[i].nFrame);
      if (v0 > 0.0f && v1 > 0.0f)
        Line(p, bx0, Y(v0), bx1, Y(v1), UiRgb(kVs, 0.85f), 1.2f * fScale);
    }
    Line(p, bx0, Y(pB[i - 1].fMeanMs), bx1, Y(pB[i].fMeanMs), UiRgb(kThis), 1.4f * fScale);
  }
}

// the parts' bar: the last second's game frame, eleven segments, and the top three named
void PartsBar(const Pen &p, float x, float y, float w, float fScale, bool bLegend)
{
  const PortBenchSecond *pS = NULL;
  const int nS = PortBenchHistory(&pS);
  const float h = 7.2f * fScale;
  Fill(p, {x, y, w, h}, UiRgb(0x3980bc, 0.14f), 1.8f);
  if (nS == 0)
    return;
  const PortBenchSecond &s = pS[nS - 1];
  double dTotal = 0.0;
  for (int i = 0; i < PERF_PARTS; ++i)
    dTotal += s.adPartMs[i];
  if (dTotal <= 0.0)
    return;
  float bx = x;
  for (int i = 0; i < PERF_PARTS; ++i) {
    const float bw = static_cast<float>(w * s.adPartMs[i] / dTotal);
    if (bw > 0.3f)
      Fill(p, {bx, y, bw, h}, UiRgb(kParts[i]));
    bx += bw;
  }
  if (!bLegend)
    return;
  int anTop[3] = {-1, -1, -1};
  for (int k = 0; k < 3; ++k)
    for (int i = 0; i < PERF_PARTS; ++i) {
      if (i == anTop[0] || i == anTop[1])
        continue;
      if (anTop[k] < 0 || s.adPartMs[i] > s.adPartMs[anTop[k]])
        anTop[k] = i;
    }
  const float ly = y + h + 13.0f * fScale, fSize = 10.8f * fScale;
  // the top three as they fit, the total at the right when there is room for it
  const std::string total = PortTextF("{0} ms a frame", {N2(dTotal)});
  const float fTotalW = Width(p, fSize, total.c_str());
  const bool bTotal = w >= 300.0f * fScale;
  const float fEnd = x + w - (bTotal ? fTotalW + 10.0f * fScale : 0.0f);
  float lx = x;
  for (int k = 0; k < 3; ++k) {
    const std::string item = std::string(PortBenchPartName(anTop[k])) + " " + N2(s.adPartMs[anTop[k]]);
    if (lx + 10.0f * fScale + Width(p, fSize, item.c_str()) > fEnd)
      break;
    Fill(p, {lx, ly - 7.0f * fScale, 7.0f * fScale, 7.0f * fScale}, UiRgb(kParts[anTop[k]]));
    lx += 10.0f * fScale;
    lx += Text(p, lx, ly, fSize, kLbInkDesc, item.c_str()) + 10.0f * fScale;
  }
  if (bTotal)
    TextRight(p, x + w, ly, fSize, kLbInkDesc, total.c_str());
}

// The readout: the run's head and progress, the frames a second large in their health's colour,
// a row each for the figures, the last minute, a frame by part, and the keys.
Rect g_readout;  // as drawn, stage units, for the card to keep clear of it

void Readout(const Pen &p, const Layout &l)
{
  const PortBenchOverlay &o = Ov();
  const float s = o.bLarge ? 1.6f : 1.0f;
  const Figures f = FiguresNow();
  const unsigned nFig = o.nFigures;
  const bool bOnly = o.nDensity == 2;
  const Rect &g = l.game;
  const PortBenchSettings &set = *PortBenchCurrent();
  const unsigned nFrame = PortBenchGameFrame(), nTarget = static_cast<unsigned>(set.dwFrames);
  const unsigned nHealth = HealthRgb(f);
  if (bOnly) {
    // one line of the figures chosen
    std::vector<std::pair<std::string, std::string>> cells;
    if (nFig & PORT_FIG_FPS)
      cells.push_back({f.bHave && !NoPicture() ? N0(f.dFps) : kDash, PortText("fps")});
    if (nFig & PORT_FIG_LOW)
      cells.push_back({f.dLow > 0.0 ? N0(f.dLow) : kDash, PortText("1 % low")});
    if (nFig & PORT_FIG_MS)
      cells.push_back({f.dMs > 0.0 ? N2(f.dMs) : kDash, PortText("ms")});
    if (nFig & PORT_FIG_GFPS)
      cells.push_back({f.bHave ? N1(f.dGfps) : kDash, PortText("game / s")});
    if (nFig & PORT_FIG_UNITS)
      cells.push_back({f.bHave ? N0(f.nUnits) : kDash, PortText("units")});
    // {0} the game time the run ends at
    if (nFig & PORT_FIG_CLOCK)
      cells.push_back({GameClock(nFrame), PortTextF("of {0}", {GameClock(nTarget)})});
    if (nFig & PORT_FIG_VS)
      cells.push_back({VsText(f), f.vsNote});
    float w = 25.2f * s;
    for (auto &c : cells)
      w += Width(p, 19.8f * s, c.first.c_str(), UI_FACE_BOLD) + Width(p, 11.7f * s, c.second.c_str()) + 25.2f * s;
    const float h = 40.0f * s;
    const bool bRight = o.nCorner == 1 || o.nCorner == 3, bBottom = o.nCorner >= 2;
    const Rect r = {bRight ? g.x + g.w - 14.4f - w : g.x + 14.4f, bBottom ? g.y + g.h - 14.4f - h : g.y + 14.4f, w, h};
    g_readout = r;
    Panel(p, r);
    float x = r.x + 12.6f * s;
    const float y = r.y + h / 2.0f + 7.0f * s;
    for (size_t i = 0; i < cells.size(); ++i) {
      const bool bFps = (nFig & PORT_FIG_FPS) && i == 0;
      x += Text(p, x, y, (bFps ? 24.0f : 19.8f) * s, bFps ? UiRgb(nHealth) : kLbInkInfo, cells[i].first.c_str(),
                UI_FACE_BOLD) + 5.0f * s;
      x += Text(p, x, y, 11.7f * s, kLbInkDesc, cells[i].second.c_str()) + 20.0f * s;
    }
    return;
  }
  const float w = 306.0f * s, pad = 12.6f * s;
  // the height: head, progress, the big line, the rows, the last minute, the parts, the keys
  int nRows = 0;
  nRows += (nFig & PORT_FIG_MS) ? 1 : 0;
  nRows += (nFig & PORT_FIG_GFPS) ? 1 : 0;
  nRows += (nFig & PORT_FIG_UNITS) ? 1 : 0;
  nRows += (nFig & PORT_FIG_VS) ? 1 : 0;
  nRows += (nFig & PORT_FIG_LOW) && !(nFig & PORT_FIG_FPS) ? 1 : 0;
  const bool bGraph = Shows(PORT_OV_GRAPH), bParts = Shows(PORT_OV_PARTS), bKeys = o.nDensity == 0;
  float h = pad + 14.0f * s + 9.0f * s;
  if (nFig & PORT_FIG_FPS)
    h += 52.0f * s;
  h += nRows * 21.0f * s + (bGraph ? 6.0f * s + 63.0f * s + 14.0f * s : 0.0f) +
       (bParts ? 12.0f * s + 7.2f * s + 16.0f * s : 0.0f) + (bKeys ? 20.0f * s : 0.0f) + pad;
  const bool bRight = o.nCorner == 1 || o.nCorner == 3, bBottom = o.nCorner >= 2;
  const Rect r = {bRight ? g.x + g.w - 14.4f - w : g.x + 14.4f, bBottom ? g.y + g.h - 14.4f - h : g.y + 14.4f, w, h};
  g_readout = r;
  Panel(p, r);
  const float x = r.x + pad, iw = w - 2.0f * pad;
  float y = r.y + pad + 11.0f * s;
  // the head: a dot for the state, the run, the clock
  const State st = RunState();
  Fill(p, {x, y - 8.0f * s, 8.1f * s, 8.1f * s},
       st == STATE_RUNNING ? UiRgb(kGood) : st == STATE_PAUSED ? kLbHyperionRim : kScInkOff, 4.0f * s);
  const std::string clock = GameClock(nFrame) + " / " + GameClock(nTarget);
  const float fClockW = Width(p, 11.7f * s, clock.c_str());
  TextIn(p, x + 14.0f * s, y, iw - 14.0f * s - fClockW - 8.0f, 11.7f * s, kLbInkDesc,
         PortText("BENCHMARK") + std::string(kDot) + MapTitle());
  TextRight(p, x + iw, y, 11.7f * s, kLbInkInfo, clock.c_str());
  y += 6.0f * s;
  Fill(p, {x, y, iw, 2.7f * s}, UiRgb(0x3980bc, 0.2f));
  if (nTarget > 0)
    Fill(p, {x, y, iw * SDL_min(1.0f, static_cast<float>(nFrame) / nTarget), 2.7f * s}, UiRgb(kThis));
  y += 3.0f * s;
  // the big line
  if (nFig & PORT_FIG_FPS) {
    y += 46.0f * s;
    const std::string fps = NoPicture() || !f.bHave ? std::string(kDash) : N0(f.dFps);
    const float fFpsW = Text(p, x, y, 48.6f * s, UiRgb(nHealth), fps.c_str(), UI_FACE_BOLD);
    Text(p, x + fFpsW + 6.0f * s, y, 14.4f * s, kLbInkDesc, "FPS", UI_FACE_WIDE);
    // {0} the frames a second at the worst hundredth
    if (nFig & PORT_FIG_LOW)
      TextRight(p, x + iw, y, 13.5f * s, kLbInkInfo,
                PortTextF("1 % low {0}", {f.dLow > 0.0 ? N0(f.dLow) : kDash}).c_str());
    y += 6.0f * s;
  }
  auto Row = [&](const char *pszLabel, const std::string &value, const std::string &note, UiColor ink) {
    y += 21.0f * s;
    Text(p, x, y, 12.6f * s, kLbInkDesc, pszLabel);
    Text(p, x + 72.0f * s, y, 16.2f * s, ink, value.c_str(), UI_FACE_BOLD);
    TextRight(p, x + iw, y, 11.7f * s, kLbInkDesc, note.c_str());
  };
  // {0} the ms a frame took at its worst
  if (nFig & PORT_FIG_MS)
    Row(PortText("Frame"), f.dMs > 0.0 ? PortTextF("{0} ms", {N2(f.dMs)}) : kDash,
        f.dWorstMs > 0.0 ? PortTextF("worst {0}", {N2(f.dWorstMs)}) : "", kLbInkInfo);
  if (nFig & PORT_FIG_GFPS)
    Row(PortText("Game"), f.bHave ? N1(f.dGfps) : kDash, f.held, kLbInkInfo);
  // {0} the players still in, {1} the players the run began with
  if (nFig & PORT_FIG_UNITS)
    Row(PortText("Units"), f.bHave ? N0(f.nUnits) : kDash,
        PortTextF("{0} of {1} left", {std::to_string(g_nPlayersLeft), std::to_string(set.nPlayers)}), kLbInkInfo);
  if (nFig & PORT_FIG_VS)
    Row(PortText("Against"), VsText(f), f.vsNote, f.bVs ? UiRgb(nHealth) : kLbInkInfo);
  if ((nFig & PORT_FIG_LOW) && !(nFig & PORT_FIG_FPS))
    Row(PortText("Worst 1 %"), f.dLow > 0.0 ? N0(f.dLow) : kDash, "", kLbInkInfo);
  if (bGraph) {
    y += 6.0f * s;
    Spark(p, {x, y, iw, 63.0f * s}, s);
    y += 63.0f * s + 12.0f * s;
    Text(p, x, y, 10.8f * s, kLbInkDesc,
         (std::string(f.dMs > 0.0 ? PortTextF("{0} ms", {N2(f.dMs)}) : std::string(kDash)) + kDot +
          PortText("the last minute"))
             .c_str());
  }
  if (bParts) {
    y += 8.0f * s;
    PartsBar(p, x, y, iw, s, true);
    y += 7.2f * s + 16.0f * s;
  }
  if (bKeys) {
    y += 16.0f * s;
    Text(p, x, y, 10.8f * s, kScInkOff,
         PortText("O the next overlay  \xc2\xb7  H hide  \xc2\xb7  S stop  \xc2\xb7  Esc set-up"));
  }
}

// The strip: the figures in cells along the picture's top or foot, the last minute, the parts.
Rect g_strip;

void Strip(const Pen &p, const Layout &l)
{
  const PortBenchOverlay &o = Ov();
  const Figures f = FiguresNow();
  const Rect &g = l.game;
  const float h = o.nDensity == 2 ? 45.0f : o.nDensity == 1 ? 72.0f : 86.4f;
  const Rect r = {g.x, o.nEdge == 0 ? g.y : g.y + g.h - h, g.w, h};
  g_strip = r;
  AddCover(p, r);
  Fill(p, r, UiRgb(0x030c18, SeeAlpha()));
  Fill(p, {r.x, o.nEdge == 0 ? r.y + r.h - 1.8f : r.y, r.w, 1.8f}, kLbInnerRim);
  const PortBenchSettings &set = *PortBenchCurrent();
  const unsigned nFrame = PortBenchGameFrame(), nTarget = static_cast<unsigned>(set.dwFrames);
  const bool bInline = o.nDensity == 2;
  float x = r.x + 19.8f;
  const float cy = r.y + r.h / 2.0f;
  // the state chip
  const State st = RunState();
  const char *pszState = st == STATE_RUNNING  ? PortText("RUNNING")
                         : st == STATE_PAUSED ? PortText("PAUSED")
                         : st == STATE_DONE   ? PortText("DONE")
                                              : PortText("LOADING");
  const float fChipW = Width(p, 10.8f, pszState, UI_FACE_WIDE) + 16.0f;
  const Rect chip = {x, cy - 11.0f, fChipW, 22.0f};
  Fill(p, chip, st == STATE_RUNNING ? UiRgb(kGood) : st == STATE_PAUSED ? UiRgb(kGold) : kLbItem, 2.7f);
  Text(p, x + 8.0f, cy + 4.0f, 10.8f, st == STATE_RUNNING || st == STATE_PAUSED ? UiRgb(0x000000) : kLbInkInfo, pszState,
       UI_FACE_WIDE);
  x += fChipW + 19.8f;
  auto Cell = [&](const std::string &value, const char *pszLabel, bool bFps, UiColor ink) {
    if (bInline) {
      x += Text(p, x, cy + 7.0f, bFps ? 23.4f : 19.8f, ink, value.c_str(), UI_FACE_BOLD) + 5.0f;
      x += Text(p, x, cy + 7.0f, 10.8f, kLbInkDesc, pszLabel) + 19.8f;
      return;
    }
    const float fBig = bFps ? 36.0f : 25.2f;
    const float vw = Text(p, x, cy + (o.nDensity == 1 ? 8.0f : 6.0f), fBig, ink, value.c_str(), UI_FACE_BOLD);
    const float lw = Text(p, x, cy + (o.nDensity == 1 ? 24.0f : 26.0f), 10.8f, kLbInkDesc, pszLabel);
    x += SDL_max(vw, lw) + 19.8f;
  };
  const unsigned nFig = o.nFigures;
  if (nFig & PORT_FIG_FPS)
    Cell(f.bHave && !NoPicture() ? N0(f.dFps) : kDash, PortText("FRAMES A SECOND"), true, UiRgb(0x66d5fe));
  if (nFig & PORT_FIG_LOW)
    Cell(f.dLow > 0.0 ? N0(f.dLow) : kDash, PortText("WORST 1 %"), false, kLbInkInfo);
  if (nFig & PORT_FIG_MS)
    Cell(f.dMs > 0.0 ? N2(f.dMs) : kDash, PortText("MS A FRAME"), false, kLbInkInfo);
  if (nFig & PORT_FIG_GFPS)
    Cell(f.bHave ? N1(f.dGfps) : kDash, PortText("GAME FRAMES A SECOND"), false, kLbInkInfo);
  // {0} the players still in, {1} the players the run began with
  if (nFig & PORT_FIG_UNITS)
    Cell(f.bHave ? N0(f.nUnits) : kDash,
         (PortText("UNITS") + std::string(kDot) +
          PortTextF("{0} OF {1} LEFT", {std::to_string(g_nPlayersLeft), std::to_string(set.nPlayers)}))
             .c_str(),
         false, kLbInkInfo);
  // {0} the game time the run ends at
  if (nFig & PORT_FIG_CLOCK)
    Cell(GameClock(nFrame), PortTextF("GAME TIME OF {0}", {GameClock(nTarget)}).c_str(), false, kLbInkInfo);
  // {0} the build the run is held against
  if (nFig & PORT_FIG_VS)
    Cell(VsText(f), f.bVs ? PortTextF("AGAINST {0}", {g_vs.build}).c_str() : PortText("NO EARLIER RUN"), false,
         f.bVs ? UiRgb(HealthRgb(f)) : kLbInkInfo);
  if (bInline)
    return;
  // the last minute, then the parts, in what is left
  const float fPartsW = SDL_clamp((r.x + r.w - 19.8f - x) * 0.3f, 153.0f, 270.0f);
  const float fSparkW = r.x + r.w - 19.8f - x - (Shows(PORT_OV_PARTS) ? fPartsW + 19.8f : 0.0f);
  const float fSparkH = o.nDensity == 1 ? 46.8f : 57.6f;
  if (Shows(PORT_OV_GRAPH) && fSparkW > 80.0f)
    Spark(p, {x, cy - fSparkH / 2.0f, fSparkW, fSparkH}, 1.0f);
  if (Shows(PORT_OV_PARTS))
    PartsBar(p, r.x + r.w - 19.8f - fPartsW, cy - 10.0f, fPartsW, 1.2f, true);
}

// The controls along the top: always with the panels and in a window; over the game with a
// readout, a strip or nothing, while the run is not running, the summary is up, or for 2.5 s
// after the mouse moves.
bool ControlsShown(void)
{
  if (g_nShown == SHOWN_WINDOW || Ov().nLayout == 0)
    return true;
  return RunState() != STATE_RUNNING || g_bSummary || g_bAskLeave || SDL_GetTicks() < g_nMovedAt + 2500;
}

// O: Full overlay, Large readings, Minimal, and round again; from a strip or a small readout to
// Large readings.
void OverlayNext(void)
{
  PortBenchOverlay &o = PortBenchOverlayLive();
  if (o.nLayout == 0) {
    o.nLayout = 1;
    o.bLarge = true;
  } else if (o.nLayout == 1 && o.bLarge) {
    o.nLayout = 3;
  } else if (o.nLayout == 3) {
    o.nLayout = 0;
  } else {
    o.nLayout = 1;
    o.bLarge = true;
  }
  g_bHidden = false;
  if (RunState() == STATE_RUNNING || RunState() == STATE_PAUSED)
    PortBenchOverlayChanged("key");
  const bool bWindow = g_nShown == SHOWN_WINDOW;
  Toast(o.nLayout == 0 ? (bWindow ? PortText("The panels round the window. O for the next")
                                  : PortText("Full overlay. O for the next"))
        : o.nLayout == 1 ? PortText("Large readings. O for the next")
        : bWindow        ? PortText("Nothing round the window. O for the next")
                         : PortText("Minimal: the game alone, H for a counter. O for the next"));
}

// The Overlay dialog during a run: the layout, where, how much, the see-through; Use Now writes
// the change to the run's log.
bool g_bOvDialog;
PortBenchOverlay g_ovDraft;

void OverlayDialog(const Pen &p, const Layout &l)
{
  const float w = 760.0f, h = 380.0f;
  const Rect box = {(l.fW - w) / 2.0f, 135.0f, w, h};
  Fill(p, {0.0f, 0.0f, l.fW, l.fH}, UiRgb(0x000000, 0.35f));
  AddCover(p, {0.0f, 0.0f, l.fW, l.fH});
  Fill(p, box, UiRgb(0x030b16, 0.97f), 4.0f);
  Ring(p, box, kScRim, 4.0f, 1.8f);
  Text(p, box.x + 24.0f, box.y + 38.0f, 21.6f, kLbInkInfo, PortText("Overlay"), UI_FACE_WIDE);
  const PortBenchOverlay &d = g_ovDraft;
  float y = box.y + 78.0f;
  const float lx = box.x + 24.0f, cx = box.x + 190.0f;
  auto Choices = [&](const char *pszRow, const char *const *ppItems, int nItems, int nOn, int nRow) {
    Text(p, lx, y + 20.0f, 13.5f, kLbInkDesc, pszRow);
    float bx = cx;
    for (int i = 0; i < nItems; ++i)
      bx += Button(p, bx, y, 30.0f, 12.6f, ppItems[i], ACT_OVSET, i == nOn ? LOOK_PRESSED : LOOK_PLAIN, false, 0.0f,
                   nRow * 16 + i) + 5.0f;
    y += 42.0f;
  };
  const char *const kLayouts[4] = {PortText("Panels"), PortText("A readout"), PortText("A strip"),
                                   PortText("Nothing")};
  Choices(PortText("Overlay"), kLayouts, 4, d.nLayout, 0);
  if (d.nLayout == 1) {
    const char *const kCorners[6] = {PortText("Top left"),     PortText("Top right"), PortText("Bottom left"),
                                     PortText("Bottom right"), PortText("Small"),     PortText("Large")};
    Text(p, lx, y + 20.0f, 13.5f, kLbInkDesc, PortText("Where"));
    float bx = cx;
    for (int i = 0; i < 6; ++i)
      bx += Button(p, bx, y, 30.0f, 12.6f, kCorners[i], ACT_OVSET,
                   (i < 4 ? i == d.nCorner : (i == 5) == d.bLarge) ? LOOK_PRESSED : LOOK_PLAIN, false, 0.0f,
                   1 * 16 + i) + (i == 3 ? 16.0f : 5.0f);
    y += 42.0f;
  } else if (d.nLayout == 2) {
    const char *const kEdges[2] = {PortText("The top"), PortText("The foot")};
    Choices(PortText("Where"), kEdges, 2, d.nEdge, 2);
  } else {
    Text(p, lx, y + 20.0f, 13.5f, kLbInkDesc, PortText("Where"));
    Text(p, cx, y + 20.0f, 13.5f, kLbInkInfo,
         d.nLayout == 0 ? (g_nShown == SHOWN_WINDOW ? PortText("Round the window, off the picture")
                                                    : PortText("Down the sides, the graph along the foot"))
                        : PortText("Only the controls, on a move of the mouse"));
    y += 42.0f;
  }
  if (d.nLayout != 3) {
    const char *const kDensities[3] = {PortText("Full"), PortText("Compact"), PortText("Figures only")};
    Choices(PortText("How much"), kDensities, 3, d.nDensity < 3 ? d.nDensity : -1, 3);
  }
  const char *const kSees[4] = {PortText("None"), "30 %", "50 %", "75 %"};
  const int nSee = PortBenchOverlaySee(d, PortBenchCurrent()->nShown);
  Choices(PortText("See-through"), kSees, 4, nSee == 0 ? 0 : nSee == 30 ? 1 : nSee == 50 ? 2 : nSee == 75 ? 3 : -1, 4);
  TextWrap(p, lx, y + 8.0f, w - 48.0f, 12.6f, kLbInkDesc,
           PortText("The game draws the overlay, and its time is in a frame\xe2\x80\x99s ui part. A change now is written in "
                    "seconds.csv, and the run compares as the overlay it began with."),
           3);
  const float bh = 38.0f, bwMin = 160.0f;
  float bx = box.x + w - 24.0f - 2 * bwMin - 10.0f;
  const float by = box.y + h - 20.0f - bh;
  bx += Button(p, bx, by, bh, 14.4f, PortText("Use Now"), ACT_OVUSE, LOOK_GOLD, false, bwMin, 0, 'U') + 10.0f;
  Button(p, bx, by, bh, 14.4f, PortText("Cancel"), ACT_OVCANCEL, LOOK_PLAIN, false, bwMin, 0, 'C');
}

void OverlaySetChoice(int n)
{
  PortBenchOverlay &d = g_ovDraft;
  const int nRow = n / 16, i = n % 16;
  switch (nRow) {
    case 0: d.nLayout = i; break;
    case 1:
      if (i < 4)
        d.nCorner = i;
      else
        d.bLarge = i == 5;
      break;
    case 2: d.nEdge = i; break;
    case 3: d.nDensity = i; break;
    case 4: d.nSee = i == 0 ? 0 : i == 1 ? 30 : i == 2 ? 50 : 75; break;
    default: break;
  }
}

// The run's view as the overlay has it, after the game's picture.
void DrawOverlay(const Pen &p, const Layout &l)
{
  const PortBenchOverlay &o = Ov();
  if (o.nLayout == 0 || NoPicture()) {
    if (ControlsShown())
      TopStrip(p, l);
    if (g_nShown == SHOWN_FULL && !Shows(PORT_OV_PLAYERS | PORT_OV_SETTINGS) && !NoPicture()) {
      // nothing down the left
    } else {
      LeftPanel(p, l);
    }
    RightPanel(p, l);
    if (Shows(PORT_OV_GRAPH) || NoPicture())
      Foot(p, l);
    if (l.sel.w > 0.0f)
      SelectionPanel(p, l.sel);
    if (l.bMiniBox && Shows(PORT_OV_MINIMAP)) {
      Panel(p, l.mini);
      g_miniBox = {l.mini.x + 5.4f, l.mini.y + 5.4f, l.mini.w - 10.8f, l.mini.h - 10.8f};
    } else if (l.bMiniBox) {
      g_miniBox = {0, 0, 0, 0};
    }
    Minimap(p, g_miniBox);
    return;
  }
  g_miniBox = {0, 0, 0, 0};
  g_readout = g_strip = {0, 0, 0, 0};
  if (o.nLayout == 1) {
    Readout(p, l);
    // the card in the foot corner the readout leaves free
    const Rect &g = l.game;
    const bool bLeftFree = o.nCorner != 2;
    SelectionCard(p, bLeftFree ? g.x + 14.4f : g.x + g.w - 14.4f - 450.0f, g.y + g.h - 14.4f);
  } else if (o.nLayout == 2) {
    Strip(p, l);
    const Rect &g = l.game;
    SelectionCard(p, g.x + 14.4f, o.nEdge == 1 ? g_strip.y - 10.8f : g.y + g.h - 14.4f);
  }
  if (ControlsShown()) {
    // over the game, beside a readout in a top corner or under a strip along the top
    Layout lc = l;
    if (g_nShown == SHOWN_FULL && o.nLayout == 1 && o.nCorner <= 1 && g_readout.w > 0.0f) {
      if (o.nCorner == 1) {
        lc.top.w = g_readout.x - 12.0f - lc.top.x;
      } else {
        lc.top.x = g_readout.x + g_readout.w + 12.0f;
        lc.top.w = l.fW - 16.0f - lc.top.x;
      }
    } else if (g_nShown == SHOWN_FULL && o.nLayout == 2 && o.nEdge == 0) {
      lc.top.y = g_strip.y + g_strip.h + 8.0f;
    }
    TopStrip(p, lc);
  }
}

// The summary, over the game at the run's end.
void Summary(const Pen &p, const Layout &l)
{
  const float w = 936.0f, h = 560.0f;
  const Rect box = {l.game.x + (l.game.w - w) / 2.0f, l.game.y + (l.game.h - h) / 2.0f, w, h};
  Fill(p, {0.0f, 0.0f, l.fW, l.fH}, UiRgb(0x000000, 0.35f));
  Fill(p, box, kScFillSolid, 4.0f);
  Ring(p, box, kScRim, 4.0f, 1.8f);
  const std::string ended = PortBenchEndReason();
  const char *pszTitle = ended == "finished" ? PortText("Benchmark Finished")
                         : ended == "last"   ? PortText("Benchmark Over: One Player Left")
                                             : PortText("Benchmark Stopped");
  const float fTitleW = Width(p, 24.0f, pszTitle, UI_FACE_WIDE);
  Text(p, box.x + (w - fTitleW) / 2.0f, box.y + 40.0f, 24.0f, kLbInkInfo, pszTitle, UI_FACE_WIDE);
  const PortBenchSettings &s = *PortBenchCurrent();
  const unsigned nFrame = PortBenchGameFrame();
  const PortBenchSecond *pS = NULL;
  const int nS = PortBenchHistory(&pS);
  // {0} how far into the game it ran
  const std::string line = MapTitle() + kDot +
                           PortTextNF("{0} player", "{0} players", s.nPlayers, {std::to_string(s.nPlayers)}) + kDot +
                           SpeedLabel() + kDot + PortTextF("{0} of game", {GameClock(nFrame)});
  Text(p, box.x + (w - Width(p, 14.4f, line.c_str())) / 2.0f, box.y + 68.0f, 14.4f, kLbInkDesc, line.c_str());
  // the four figures and the parts: what summary.json says
  static std::string s_of;
  static PortJson s_sum;
  if (s_of != std::string(PortBenchFolder()) + "|" + ended) {
    s_of = std::string(PortBenchFolder()) + "|" + ended;
    s_sum = PortJson();
    PortJsonParse(PortReadText(std::string(PortBenchFolder()) + "\\summary.json").c_str(), &s_sum);
  }
  double dParts[PERF_PARTS] = {};
  for (int i = 0; i < PERF_PARTS; ++i)
    dParts[i] = s_sum["ms_per_game_frame"][PortBenchPartName(i)].Num();
  const unsigned nMost = static_cast<unsigned>(s_sum["units_most"].Num());
  const unsigned nMostAt = static_cast<unsigned>(s_sum["units_most_at"].Num());
  ReadAgainst();
  const bool bVs = HasAgainst();
  double dVsParts[PERF_PARTS] = {}, dVsFrames = 0.0;
  if (bVs) {
    const int nTo = AgainstSecond(nFrame);
    for (int k = 0; k <= nTo && k < static_cast<int>(g_vs.secParts.size()); ++k) {
      const float fGf = k > 0 ? g_vs.secFrame[k] - g_vs.secFrame[k - 1] : g_vs.secFrame[0];
      for (int i = 0; i < PERF_PARTS; ++i)
        dVsParts[i] += g_vs.secParts[k][i] * fGf;
      dVsFrames += fGf;
    }
  }
  const float fw = (w - 60.0f - 3 * 10.0f) / 4.0f;
  const int nFirstFigure = NoPicture() ? 2 : 0;
  float fx = box.x + 30.0f + nFirstFigure * (fw + 10.0f) / 2.0f;
  const float fy = box.y + 88.0f;
  const std::string figures[4][2] = {
      {PortText("Frames a second"), N0(s_sum["frames_per_s"].Num())},
      {PortText("At the worst 1 %"), N0(s_sum["frames_per_s_1pct_low"].Num())},
      {PortText("Game frames a second"), N1(s_sum["game_frames_per_s"].Num())},
      {PortText("Most units"), N0(nMost)},
  };
  for (int i = nFirstFigure; i < 4; ++i) {
    const Rect r = {fx, fy, fw, 76.0f};
    Fill(p, r, kLbItem, 2.7f);
    Ring(p, r, kLbItemRim, 2.7f, 0.9f);
    Text(p, r.x + 12.0f, r.y + 22.0f, 11.7f, kLbInkDesc, figures[i][0].c_str());
    Text(p, r.x + 12.0f, r.y + 56.0f, 25.2f, i == 0 ? UiRgb(kThis) : kLbInkInfo, figures[i][1].c_str(), UI_FACE_BOLD);
    fx += fw + 10.0f;
  }
  // the parts over the run, against the other run's to the same moment
  const float cx = box.x + 30.0f, cw = (w - 60.0f - 24.0f) * 1.2f / 2.2f;
  float cy = fy + 76.0f + 30.0f;
  Title(p, cx, cy, PortText("A GAME FRAME, BY PART"));
  double dMost = 0.01;
  for (int i = 0; i < PERF_PARTS; ++i) {
    dVsParts[i] = dVsFrames > 0.0 ? dVsParts[i] / dVsFrames : 0.0;
    dMost = SDL_max(dMost, SDL_max(dParts[i], dVsParts[i]));
  }
  cy += 4.0f;
  for (int i = 0; i < PERF_PARTS; ++i) {
    cy += 21.0f;
    PartRow(p, cx, cy, cw, i, dParts[i], dMost, dVsParts[i], bVs);
  }
  cy += 22.0f;
  Text(p, cx, cy, 11.7f, kScInkOff,
       bVs ? PortText("ms a game frame, averaged over the run; the mark is the other run")
           : PortText("ms a game frame, averaged over the run"));
  // beside them: what it is held against, the log, the most units
  const float sx = cx + cw + 24.0f, sw = box.x + w - 30.0f - sx;
  float sy = fy + 76.0f + 30.0f;
  Title(p, sx, sy, PortText("HELD AGAINST"));
  sy += 22.0f;
  // {0} when the earlier run was made, {1} its build, {2} the moment both reached
  sy += TextWrap(p, sx, sy, sw, 13.5f, kLbInkInfo,
                 bVs ? PortTextF("{0}, build {1}: the same game, up to the same moment, game {2}.",
                                 {g_vs.when, g_vs.build, GameClock(nFrame)})
                     : PortText("No earlier run of this game: this one is what the next is held against.")) -
        19.0f;
  // whether the two builds played the same game: their states at the same end frame
  const std::string state = s_sum["end_state"].Str();
  const unsigned nEndFrame = static_cast<unsigned>(s_sum["end_frame"].Num());
  if (bVs && ended != "stopped" && g_vs.ended != "stopped" && !state.empty() && !g_vs.endState.empty() &&
      nEndFrame == g_vs.nEndFrame) {
    sy += 19.0f;
    // {0} the state both games ended in, {1} the other run's when they differ
    if (state == g_vs.endState)
      sy += TextWrap(p, sx, sy, sw, 12.6f, kLbInkDesc,
                     PortTextF("Both ended in state {0}, so the two builds played the same game frame for frame.",
                               {state})) -
            19.0f;
    else
      sy += TextWrap(p, sx, sy, sw, 12.6f, UiRgb(0xff7a6a),
                     PortTextF("The two ended in different states, {0} and {1}: the builds played different games.",
                               {state, g_vs.endState})) -
            19.0f;
  }
  sy += 30.0f;
  Title(p, sx, sy, PortText("THE LOG"));
  sy += 22.0f;
  TextIn(p, sx, sy, sw, 12.6f, kLbInkInfo, FolderShown());
  sy += 19.0f;
  // {0} how many lines seconds.csv holds
  sy += TextWrap(p, sx, sy, sw, 12.6f, kLbInkDesc,
                 std::string("settings.json") + kDot +
                     PortTextF("seconds.csv ({0} lines)", {std::to_string(nS)}) + kDot + "summary.json" +
                     (s.bFramesCsv ? std::string(kDot) + "frames.csv" : "") + kDot + PortText("the replay")) -
        19.0f;
  sy += 30.0f;
  Title(p, sx, sy, PortText("MOST UNITS"));
  sy += 22.0f;
  // {0} the most units at once, {1} the game time it was at
  Text(p, sx, sy, 13.5f, kLbInkInfo,
       PortTextF("{0} at game {1}", {N0(nMost), GameClock(nMostAt)}).c_str());
  // the buttons
  const float bh = 40.0f, bwMin = 198.0f;
  const float total = 3 * bwMin + 2 * 10.0f;
  float bx = box.x + (w - total) / 2.0f;
  const float by = box.y + h - 22.0f - bh;
  bx += Button(p, bx, by, bh, 14.4f, PortText("Run Again"), ACT_AGAIN, LOOK_GOLD, false, bwMin, 0, 'A') + 10.0f;
  bx += Button(p, bx, by, bh, 14.4f, PortText("Runs"), ACT_RUNS, LOOK_PLAIN, false, bwMin, 0, 'U') + 10.0f;
  Button(p, bx, by, bh, 14.4f, PortText("Close"), ACT_CLOSE, LOOK_PLAIN, false, bwMin, 0, 'C');
}

// Set-up while the run goes on: "Stop the benchmark?"
void AskLeave(const Pen &p, const Layout &l)
{
  const float w = 560.0f, h = 210.0f;
  const Rect box = {l.game.x + (l.game.w - w) / 2.0f, l.game.y + (l.game.h - h) / 2.0f, w, h};
  Fill(p, {0.0f, 0.0f, l.fW, l.fH}, UiRgb(0x000000, 0.35f));
  Fill(p, box, kScFillSolid, 4.0f);
  Ring(p, box, kScRim, 4.0f, 1.8f);
  const char *pszTitle = PortText("Stop the benchmark?");
  Text(p, box.x + (w - Width(p, 21.6f, pszTitle, UI_FACE_WIDE)) / 2.0f, box.y + 44.0f, 21.6f, kLbInkInfo, pszTitle,
       UI_FACE_WIDE);
  const char *pszBody = PortText("The run stops here and its log is kept, marked stopped.");
  const char *pszBody2 = PortText("The set-up comes back as you left it.");
  Text(p, box.x + (w - Width(p, 14.4f, pszBody)) / 2.0f, box.y + 84.0f, 14.4f, kLbInkDesc, pszBody);
  Text(p, box.x + (w - Width(p, 14.4f, pszBody2)) / 2.0f, box.y + 106.0f, 14.4f, kLbInkDesc, pszBody2);
  const float bh = 40.0f, bwMin = 200.0f;
  float bx = box.x + (w - 2 * bwMin - 10.0f) / 2.0f;
  const float by = box.y + h - 22.0f - bh;
  bx += Button(p, bx, by, bh, 14.4f, PortText("Stop and Leave"), ACT_LEAVE, LOOK_GOLD, false, bwMin, 0, 'L') + 10.0f;
  Button(p, bx, by, bh, 14.4f, PortText("Keep Running"), ACT_KEEP, LOOK_PLAIN, false, bwMin, 0, 'K');
}

void Act(Action act, int n)
{
  switch (act) {
    case ACT_GO:
      if (RunState() == STATE_DONE)
        PortBenchRestart();
      else if (RunState() != STATE_LOADING)
        PortBenchStop();
      break;
    case ACT_PAUSE:
      if (RunState() == STATE_RUNNING) {
        pause_game();
        PortBenchPauseSet(true);
      } else if (RunState() == STATE_PAUSED) {
        PortBenchPauseSet(false);
        unpause_game(0);
      }
      break;
    case ACT_RESTART:
      if (RunState() != STATE_LOADING)
        PortBenchRestart();
      break;
    case ACT_HIDE: g_bHidden = true; break;
    case ACT_SHOW: g_bHidden = false; break;
    case ACT_SLOWER:
    case ACT_FASTER:
      if (RunState() == STATE_RUNNING || RunState() == STATE_PAUSED)
        PortBenchSpeedStep(act == ACT_FASTER ? 1 : -1);
      break;
    case ACT_SETUP:
      if (RunState() == STATE_DONE)
        PortBenchLeave(0);
      else
        g_bAskLeave = true;
      break;
    case ACT_LEAVE: g_bAskLeave = false; PortBenchLeave(0); break;
    case ACT_KEEP: g_bAskLeave = false; break;
    case ACT_CAMERA: CameraSet(n); break;
    case ACT_GRAPH: g_nGraph = n; break;
    case ACT_AGAIN: g_bSummary = false; PortBenchRestart(); break;
    case ACT_RUNS: g_bSummary = false; PortBenchLeave(1); break;
    case ACT_CLOSE: g_bSummary = false; g_bSummaryClosed = true; break;
    case ACT_OVERLAY:
      g_ovDraft = PortBenchOverlayLive();
      g_bOvDialog = true;
      break;
    case ACT_OVSET: OverlaySetChoice(n); break;
    case ACT_OVUSE: {
      PortBenchOverlay &o = PortBenchOverlayLive();
      const bool bChanged = memcmp(&o, &g_ovDraft, sizeof o) != 0;
      o = g_ovDraft;
      g_bOvDialog = false;
      g_bHidden = false;
      if (bChanged && (RunState() == STATE_RUNNING || RunState() == STATE_PAUSED))
        PortBenchOverlayChanged("dialog");
      break;
    }
    case ACT_OVCANCEL: g_bOvDialog = false; break;
    case ACT_LEFTTAB: g_nLeftTab = n; break;
    case ACT_PORTRAIT:
    case ACT_PLAYER: {
      // the camera to the unit, or to the player's base, and free
      int x = 0, y = 0;
      bool bAt = false;
      PortBenchUnit u;
      if (act == ACT_PORTRAIT && PortBenchUnitRead(n, &u)) {
        CUnit *pUnit = CUnit::Head() + n;
        x = pUnit->wPositionX;
        y = pUnit->wPositionY;
        bAt = true;
      } else if (act == ACT_PLAYER) {
        bAt = PlayerBase(n, &x, &y);
      }
      if (bAt && !NoPicture()) {
        if (g_nCamera != CAM_FREE)
          CameraSet(CAM_FREE);
        PortViewCameraCentre(x, y);
      }
      break;
    }
    default: break;
  }
  PortPresentRedraw();
}

// a minimap click: the camera there, and free
void MinimapAt(float fX, float fY)
{
  const Rect &m = g_miniBox;
  if (m.w <= 0.0f || gwMapWidth == 0)
    return;
  const float u = SDL_clamp((fX / g_k - m.x) / m.w, 0.0f, 1.0f), v = SDL_clamp((fY / g_k - m.y) / m.h, 0.0f, 1.0f);
  if (g_nCamera != CAM_FREE)
    CameraSet(CAM_FREE);
  PortViewCameraCentre(u * gwMapWidth * 32.0, v * gwMapHeight * 32.0);
  PortPresentRedraw();
}

}  // namespace

bool PortBenchViewShown(void)
{
  return PortBenchPlaying() && gwGameMode == 3;
}

static void LayerArea(float fW, float fH, float *pfX, float *pfY, float *pfW, float *pfH)
{
  ShownNow();
  if (!PortBenchViewShown() || g_nShown != SHOWN_WINDOW || fW <= 0.0f || fH <= 0.0f) {
    *pfX = *pfY = 0.0f;
    *pfW = fW;
    *pfH = fH;
    return;
  }
  const Layout l = LayoutFor(fW, fH);
  *pfX = l.game.x * l.k;
  *pfY = l.game.y * l.k;
  *pfW = l.game.w * l.k;
  *pfH = l.game.h * l.k;
}

static bool LayerDraw(int nWidth, int nHeight, float fX, float fY, float fW, float fH)
{
  g_nButtons = 0;
  g_nCovers = 0;
  g_bDrawn = PortBenchViewShown();
  if (!g_bDrawn)
    return false;
  g_gameBox = {fX, fY, fW, fH};
  ShownNow();
  const Layout l = LayoutFor(static_cast<float>(nWidth), static_cast<float>(nHeight));
  g_k = l.k;
  const Pen p = {l.k, g_nShown == SHOWN_FULL};
  // a new game: the camera's way applied to it, the summary closed
  if (g_nGameFrom != PortBenchGameFrame() && PortBenchGameFrame() < 2) {
    g_bCameraSet = false;
    g_bSummary = g_bSummaryClosed = g_bAskLeave = g_bOvDialog = false;
  }
  g_nGameFrom = PortBenchGameFrame();
  if (!g_bCameraSet)
    CameraApply();
  // the run's end: its summary, once
  if (RunState() == STATE_DONE && !g_bSummary && !g_bSummaryClosed)
    g_bSummary = true;
  ReadUnits();
  ReadMinimap();
  if (g_bHidden) {
    Counter(p, l);
    return true;
  }
  // round the window: the menus' black, the window's frame
  if (g_nShown == SHOWN_WINDOW) {
    const Rect all = {0.0f, 0.0f, l.fW, l.fH};
    UiPushClip(0.0f, 0.0f, static_cast<float>(nWidth), fY);
    Fill(p, all, UiRgb(0x000000));
    UiPopClip();
    WindowFrame(p, l, static_cast<int>(fW + 0.5f), static_cast<int>(fH + 0.5f));
    if (NoPicture()) {
      const Rect &g = l.game;
      Fill(p, g, UiRgb(0x000000));
      UiGradient(g.x * p.k, g.y * p.k, g.w * p.k, g.h / 2.0f * p.k, UiRgb(0x000000), UiRgb(0x124070, 0.35f), false);
      UiGradient(g.x * p.k, (g.y + g.h / 2.0f) * p.k, g.w * p.k, g.h / 2.0f * p.k, UiRgb(0x124070, 0.35f),
                 UiRgb(0x000000), false);
      const char *pszHead = PortText("NO PICTURE");
      Text(p, g.x + (g.w - Width(p, 23.4f, pszHead, UI_FACE_WIDE)) / 2.0f, g.y + g.h / 2.0f - 20.0f, 23.4f,
           kLbInkInfo, pszHead, UI_FACE_WIDE);
      const char *const apsz[2] = {
          PortText("The game runs hidden, as --bench-shown none runs it: nothing is drawn, so the"),
          PortText("numbers are the simulation\xe2\x80\x99s alone. The overlay is written once a second.")};
      for (int i = 0; i < 2; ++i)
        Text(p, g.x + (g.w - Width(p, 16.2f, apsz[i])) / 2.0f, g.y + g.h / 2.0f + 18.0f + i * 24.0f, 16.2f, kLbInkDesc,
             apsz[i]);
    }
  }
  ReadSelection();
  DrawOverlay(p, l);
  if (!g_toast.empty() && SDL_GetTicks() < g_nToastUntil) {
    const float w = Width(p, 14.4f, g_toast.c_str()) + 36.0f;
    const Rect r = {(l.fW - w) / 2.0f, 92.0f, w, 36.0f};
    Fill(p, r, UiRgb(0x030b16, 0.92f), 3.0f);
    Ring(p, r, kLbHyperionRim, 3.0f, 1.0f);
    Text(p, r.x + 18.0f, r.y + 23.0f, 14.4f, kLbInkInfo, g_toast.c_str());
  }
  if (g_bOvDialog)
    OverlayDialog(p, l);
  if (g_bSummary)
    Summary(p, l);
  else if (g_bAskLeave)
    AskLeave(p, l);
  return true;
}

// Off the game: round the game's window, over a panel or a button, or anywhere while a dialog
// is open, which the game draws its cursor under.
static bool LayerOutside(float fX, float fY)
{
  if (!PortBenchViewShown() || !g_bDrawn)
    return false;
  if (g_bSummary || g_bAskLeave || g_bOvDialog)
    return true;
  for (int i = 0; i < g_nButtons; ++i)
    if (g_buttons[i].r.In(fX, fY))
      return true;
  if (g_bHidden)
    return false;
  for (int i = 0; i < g_nCovers; ++i)
    if (g_covers[i].In(fX, fY))
      return true;
  return g_nShown == SHOWN_WINDOW && !g_gameBox.In(fX, fY);
}

static bool LayerMouse(unsigned uMsg, unsigned long long wParam, long long lParam)
{
  if (!PortBenchViewShown() || !g_bDrawn)
    return false;
  const float fX = static_cast<float>(static_cast<short>(LOWORD(lParam)));
  const float fY = static_cast<float>(static_cast<short>(HIWORD(lParam)));
  const bool bRelease = uMsg == WM_LBUTTONUP || uMsg == WM_RBUTTONUP || uMsg == WM_MBUTTONUP;
  // a dialog takes every click
  const bool bModal = g_bSummary || g_bAskLeave || g_bOvDialog;
  if (uMsg == WM_MOUSEMOVE) {
    const bool bWas = ControlsShown();
    g_nMovedAt = SDL_GetTicks();
    if (!bWas)
      PortPresentRedraw();
  }
  int nHit = -1;
  for (int i = 0; i < g_nButtons; ++i)
    if (g_buttons[i].r.In(fX, fY))
      nHit = i;
  if (bModal && nHit >= 0) {
    const Action a = g_buttons[nHit].act;
    const bool bDialogs = g_bOvDialog ? a == ACT_OVSET || a == ACT_OVUSE || a == ACT_OVCANCEL
                                      : a == ACT_AGAIN || a == ACT_RUNS || a == ACT_CLOSE || a == ACT_LEAVE ||
                                            a == ACT_KEEP;
    if (!bDialogs)
      nHit = -1;
  }
  if (nHit != g_nHot) {
    g_nHot = nHit;
    PortPresentRedraw();
  }
  if (nHit >= 0) {
    if (uMsg == WM_LBUTTONDOWN) {
      if (g_buttons[nHit].act == ACT_MINIMAP)
        MinimapAt(fX, fY);
      else
        Act(g_buttons[nHit].act, g_buttons[nHit].n);
    } else if (uMsg == WM_MOUSEMOVE && g_buttons[nHit].act == ACT_MINIMAP && (wParam & MK_LBUTTON) != 0) {
      MinimapAt(fX, fY);
    }
    return !bRelease;
  }
  if (bModal)
    return !bRelease;
  if (LayerOutside(fX, fY))
    return !bRelease;
  return false;
}

// A key going down (bAct) or the character it made; a held key's repeats are the game's.
static bool LayerKey(unsigned vk, bool bAct, bool bRepeat)
{
  if (!PortBenchViewShown() || (bAct && bRepeat))
    return false;
  // a character: a letter's as its key's, upper case
  if (!bAct && vk >= 'a' && vk <= 'z')
    vk -= 'a' - 'A';
  auto Do = [&](Action act) {
    if (bAct)
      Act(act, 0);
    return true;
  };
  if (g_bSummary) {
    switch (vk) {
      case 'A': return Do(ACT_AGAIN);
      case 'U': return Do(ACT_RUNS);
      case 'C':
      case VK_ESCAPE: return Do(ACT_CLOSE);
      default: return true;
    }
  }
  if (g_bOvDialog) {
    switch (vk) {
      case 'U':
      case VK_RETURN: return Do(ACT_OVUSE);
      case 'C':
      case VK_ESCAPE: return Do(ACT_OVCANCEL);
      default: return true;
    }
  }
  if (g_bAskLeave) {
    switch (vk) {
      case 'L': return Do(ACT_LEAVE);
      case 'K':
      case VK_ESCAPE: return Do(ACT_KEEP);
      default: return true;
    }
  }
  switch (vk) {
    case 'S': return Do(ACT_GO);
    case 'P': return Do(ACT_PAUSE);
    case 'R': return Do(ACT_RESTART);
    case 'D': return Do(ACT_SLOWER);
    case 'U': return Do(ACT_FASTER);
    case 'H': return Do(g_bHidden ? ACT_SHOW : ACT_HIDE);
    case 'O':
      if (bAct) {
        OverlayNext();
        PortPresentRedraw();
      }
      return true;
    case 'C':
      if (bAct) {
        int nWay = (g_nCamera + 1) % CAM_WAYS;
        if (CameraWayOff(nWay))
          nWay = (nWay + 1) % CAM_WAYS;
        CameraSet(nWay);
      }
      return true;
    case VK_ESCAPE: return Do(ACT_SETUP);
    default: return false;
  }
}

void PortBenchViewMenu(void)
{
  if (!PortBenchViewShown())
    return;
  g_bHidden = false;
  g_bOvDialog = false;
  if (RunState() == STATE_DONE) {
    g_bSummary = true;
    g_bSummaryClosed = false;
  } else {
    g_bAskLeave = true;
  }
  PortPresentRedraw();
}

void PortBenchViewPause(void)
{
  if (PortBenchViewShown())
    Act(ACT_PAUSE, 0);
}

void PortBenchViewWheel(void)
{
  if (PortBenchViewShown() && g_nCamera != CAM_FREE)
    CameraSet(CAM_FREE);
}

// The run's overlay as a layer over the game (port_overlay.h): the cursor free to reach its
// panels, no console (the overlay shows what it would), and its own game time and frame rate.
const PortOverlayLayer kLayer = {
    "bench", PORT_OVERLAY_BENCH, PORT_OVERLAY_FREE_CURSOR | PORT_OVERLAY_HIDES_CONSOLE | PORT_OVERLAY_OWN_COUNTERS,
    PortBenchViewShown, LayerArea, LayerDraw, NULL, LayerMouse, LayerKey, LayerOutside};
PORT_OVERLAY_LAYER(kLayer);
