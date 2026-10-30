// port_cutscene.cpp -- cutscenes scripted over the live channel (port_cutscene.h). A cutscene
// is a list of steps run in turn on the motion's clock (port_motion.cpp): each step waits for
// the one before to finish unless that one was sent with "async", so a camera glide can run
// under a caption and a fade. The camera is moved through the view's aim (port_view.cpp's
// CameraUpdate asks PortCutsceneAim each frame, as it asks a followed unit or replay TV), the
// zoom through the camera's own (PortCameraZoomSet), the picture through the motion's
// channels, the units through port_trigrun.cpp. While one plays the player's keys and buttons
// are held (event_Win.cpp) and Escape ends it; at its end the picture eases back to the
// game's in half a second.
#include <windows.h>
#include <math.h>
#include <string.h>
#include <map>
#include <string>
#include <vector>
#include <SDL3/SDL.h>
#include "../render/port_camera.h"  // PortCameraZoom, PortCameraZoomTarget, PortCameraZoomSet
#include "../render/port_present.h"  // PortPresentRedraw
#include "../render/port_view.h"  // PortViewCameraMiddle
#include "port_cutscene.h"
#include "../platform/port_json.h"
#include "../data/port_moddata.h"  // ModTypeByName
#include "../render/port_motion.h"
#include "port_trigrun.h"
#include "../front/port_ui.h"
#include "CUnit.h"
#include "CSprite.h"
#include "img.h"      // _img_list, the cursor's layer
#include "TrigAct.h"  // cur_hide, cur_show
#include "../../gen/globals117.h"
#include "../render/port_overlay.h"  // PORT_OVERLAY_LAYER

namespace {

enum Kind {
  K_CAMERA, K_FOLLOW, K_SPAWN, K_ORDER, K_KILL, K_REMOVE, K_FADE, K_BARS, K_HUD, K_DEPART, K_ARRIVE,
  K_CAPTION, K_TEXT, K_TRANSMISSION, K_SOUND, K_WAIT, K_KINDS
};
const char *const kKinds[K_KINDS] = {"camera", "follow", "spawn", "order", "kill", "remove", "fade", "bars", "hud",
                                     "depart", "arrive", "caption", "text", "transmission", "sound", "wait"};
// the steps that act on the game's units, which only a single-player game takes
bool Acts(int nKind)
{
  return nKind == K_SPAWN || nKind == K_ORDER || nKind == K_KILL || nKind == K_REMOVE;
}

struct Step {
  int nKind;
  PortJson j;
  bool bAsync;
};

struct Caption {
  std::string text;
  double t0, fLength;
  int nAt;  // 0 the bottom, 1 the middle, 2 the top
};

// The camera's move: from where it was to a place or a unit, its zoom from one to another.
struct Glide {
  bool bOn, bPlace, bZoom;
  double fFromX, fFromY, fToX, fToY, fFromZ, fToZ, t0, fLength;
  int nEase;
  int nUnit;  // the followed unit's id, -1 none
};

std::vector<Step> g_steps;
size_t g_nNext;
double g_fStepDone;  // the clock when the step waited on is done
bool g_bPlaying, g_bEnding, g_bHold, g_bSkip, g_bRest;
double g_fEndAt, g_fZoomBefore;
std::map<std::string, std::vector<int>> g_labels;
std::vector<Caption> g_captions;
Glide g_cam;
bool g_bCursorHidden;  // the game's cursor taken off while the player's input is held

double Now(void)
{
  return PortMotionClock();
}

int EaseByName(const PortJson &v, int nDefault)
{
  const std::string s = v.Str("");
  return s == "even" ? MOTION_EVEN : s == "ease" ? MOTION_EASE : s == "out" ? MOTION_OUT : s == "in_out" ? MOTION_IN_OUT
                                                                                                          : nDefault;
}

float Eased(int nEase, double u)
{
  const float f = static_cast<float>(u < 0.0 ? 0.0 : u > 1.0 ? 1.0 : u);
  switch (nEase) {
    case MOTION_EASE: return UiEase(f);
    case MOTION_OUT: return 1.0f - (1.0f - f) * (1.0f - f) * (1.0f - f);
    case MOTION_IN_OUT: return f < 0.5f ? 4.0f * f * f * f : 1.0f - 4.0f * (1.0f - f) * (1.0f - f) * (1.0f - f);
    default: return f;
  }
}

double Seconds(const PortJson &j, double fDefault)
{
  const double ms = j["ms"].Num(-1.0);
  return ms >= 0.0 ? ms / 1000.0 : fDefault;
}

// How long words stay up when not said: a second and a half and a reading pace.
double ReadingSeconds(const std::string &s)
{
  return 1.5 + 0.06 * static_cast<double>(s.size());
}

// The units a step names: a label a spawn gave ("as"), an id, or a list of them.
void UnitsOf(const PortJson &v, std::vector<int> *pIds)
{
  if (v.kind == PortJson::STRING) {
    auto it = g_labels.find(v.s);
    if (it != g_labels.end())
      pIds->insert(pIds->end(), it->second.begin(), it->second.end());
  } else if (v.kind == PortJson::NUMBER) {
    pIds->push_back(static_cast<int>(v.d));
  } else if (v.kind == PortJson::ARRAY) {
    for (size_t i = 0; i < v.items.size(); ++i)
      UnitsOf(v.items[i], pIds);
  }
}

int UnitType(const PortJson &v)
{
  return v.kind == PortJson::STRING ? ModTypeByName("units", v.s.c_str()) : static_cast<int>(v.Num(-1));
}

bool InSinglePlayer(void)
{
  return gwGameMode == 3 && gbMultiPlayerMode == 0;
}

// The view's middle and zoom now.
void CameraNow(double *pfX, double *pfY, double *pfZoom)
{
  PortViewCameraMiddle(pfX, pfY);
  *pfZoom = PortCameraZoom();
}

void GlideStart(double fSeconds, int nEase)
{
  double x, y, z;
  CameraNow(&x, &y, &z);
  // a glide from where the last one aimed, so one follows another without a jump
  if (g_cam.bOn) {
    PortCutsceneAim(&x, &y);
  }
  g_cam.fFromX = x;
  g_cam.fFromY = y;
  g_cam.fFromZ = z;
  g_cam.fToX = x;
  g_cam.fToY = y;
  g_cam.fToZ = z;
  g_cam.t0 = Now();
  g_cam.fLength = fSeconds;
  g_cam.nEase = nEase;
  g_cam.bOn = true;
  g_cam.bPlace = true;
  g_cam.bZoom = false;
  g_cam.nUnit = -1;
}

// What a step does when it comes up; how long the cutscene waits on it, in seconds.
double Run(const Step &st)
{
  const PortJson &j = st.j;
  switch (st.nKind) {
    case K_CAMERA: {
      GlideStart(Seconds(j, 1.0), EaseByName(j["ease"], MOTION_IN_OUT));
      if (j["x"].kind == PortJson::NUMBER)
        g_cam.fToX = j["x"].d;
      if (j["y"].kind == PortJson::NUMBER)
        g_cam.fToY = j["y"].d;
      if (j["zoom"].kind == PortJson::NUMBER) {
        g_cam.fToZ = j["zoom"].d;
        g_cam.bZoom = true;
      }
      return g_cam.fLength;
    }
    case K_FOLLOW: {
      std::vector<int> ids;
      UnitsOf(j["unit"], &ids);
      GlideStart(Seconds(j, 0.8), EaseByName(j["ease"], MOTION_IN_OUT));
      for (const int n : ids)
        if (PortTrigUnit(n) != NULL) {
          g_cam.nUnit = n;
          break;
        }
      if (j["zoom"].kind == PortJson::NUMBER) {
        g_cam.fToZ = j["zoom"].d;
        g_cam.bZoom = true;
      }
      return g_cam.fLength;
    }
    case K_SPAWN: {
      std::vector<int> made;
      const int x = static_cast<int>(j["x"].Num(-1)), y = static_cast<int>(j["y"].Num(-1));
      const int nPlayer = static_cast<int>(j["player"].Num(0));
      PortTrigCreate(UnitType(j["unit"]), x, y, nPlayer, static_cast<int>(j["count"].Num(1)), &made);
      const PortJson &to = j["attack"].size() == 2 ? j["attack"] : j["move"];
      if (to.size() == 2)
        for (const int n : made)
          PortTrigOrderId(n, static_cast<int>(to.At(0).Num()), static_cast<int>(to.At(1).Num()),
                          j["attack"].size() == 2 ? PORT_TRIG_ATTACK : PORT_TRIG_MOVE);
      if (j["as"].kind == PortJson::STRING) {
        std::vector<int> &label = g_labels[j["as"].s];
        label.insert(label.end(), made.begin(), made.end());
      }
      return 0.0;
    }
    case K_ORDER: {
      std::vector<int> ids;
      UnitsOf(j["units"], &ids);
      const std::string order = j["order"].Str("move");
      const int nOrder = order == "attack" ? PORT_TRIG_ATTACK : order == "patrol" ? PORT_TRIG_PATROL : PORT_TRIG_MOVE;
      for (const int n : ids)
        PortTrigOrderId(n, static_cast<int>(j["to"].At(0).Num()), static_cast<int>(j["to"].At(1).Num()), nOrder);
      return 0.0;
    }
    case K_KILL:
    case K_REMOVE: {
      std::vector<int> ids;
      UnitsOf(j["units"], &ids);
      for (const int n : ids)
        PortTrigRemoveId(n, st.nKind == K_KILL);
      return 0.0;
    }
    case K_FADE: {
      const double s = Seconds(j, 0.8);
      const int nEase = EaseByName(j["ease"], MOTION_EASE);
      const PortJson &to = j["to"];
      const float fTo = to.kind == PortJson::NUMBER ? static_cast<float>(to.d) : to.Str("black") == "clear" ? 0.0f : 1.0f;
      PortMotionTo(MOTION_BLACK, fTo, s, 0.0, nEase);
      if (j["scale"].kind == PortJson::NUMBER)  // a tenth to ten times: the camera divides by it
        PortMotionTo(MOTION_MAP_SCALE,
                     static_cast<float>(j["scale"].d < 0.1 ? 0.1 : j["scale"].d > 10.0 ? 10.0 : j["scale"].d), s, 0.0,
                     EaseByName(j["ease"], MOTION_IN_OUT));
      return s;
    }
    case K_BARS: {
      const double s = Seconds(j, 0.6);
      const bool bOn = j["on"].kind != PortJson::BOOL || j["on"].b;
      PortMotionTo(MOTION_BARS, bOn ? static_cast<float>(j["size"].Num(0.12)) : 0.0f, s, 0.0, MOTION_EASE);
      // the console goes with them unless "hud" says otherwise
      const bool bHud = j["hud"].kind == PortJson::BOOL ? j["hud"].b : !bOn;
      PortMotionTo(MOTION_HUD_ALPHA, bHud ? 1.0f : 0.0f, s, 0.0, MOTION_EASE);
      PortMotionTo(MOTION_HUD_SHIFT, bHud ? 0.0f : 0.18f, s, 0.0, MOTION_EASE);
      return s;
    }
    case K_HUD: {
      const double s = Seconds(j, 0.6);
      const bool bShow = j["show"].kind != PortJson::BOOL || j["show"].b;
      PortMotionTo(MOTION_HUD_ALPHA, bShow ? 1.0f : 0.0f, s, 0.0, MOTION_EASE);
      PortMotionTo(MOTION_HUD_SHIFT, bShow ? 0.0f : 0.18f, s, 0.0, MOTION_EASE);
      return s;
    }
    case K_DEPART: {
      const double s = Seconds(j, 1.2);
      PortMotionDepart(s);
      return s;
    }
    case K_ARRIVE: {
      PortMotionArrive();
      return PORT_MOTION_ARRIVE_SECONDS;
    }
    case K_CAPTION: {
      Caption c;
      c.text = j["text"].Str("");
      c.t0 = Now();
      c.fLength = Seconds(j, ReadingSeconds(c.text));
      const std::string at = j["at"].Str("bottom");
      c.nAt = at == "middle" ? 1 : at == "top" ? 2 : 0;
      g_captions.push_back(c);
      return c.fLength;
    }
    case K_TEXT: {
      const std::string text = j["text"].Str("");
      PortTrigText(text.c_str(), static_cast<unsigned>(j["ms"].Num(0)));
      return 0.0;
    }
    case K_TRANSMISSION: {
      const std::string text = j["text"].Str("");
      const std::string sound = j["sound"].Str("");
      std::vector<int> ids;
      UnitsOf(j["unit"], &ids);
      const double s = Seconds(j, ReadingSeconds(text));
      const PortJson &portrait = j["portrait"];
      const int nPortrait = portrait.kind == PortJson::NONE && !ids.empty() && PortTrigUnit(ids[0]) != NULL
                                ? PortTrigUnit(ids[0])->wType
                                : UnitType(portrait);
      PortTrigTransmission(nPortrait, ids.empty() ? -1 : ids[0], text.c_str(), sound.c_str(),
                           static_cast<unsigned>(s * 1000.0));
      // the console's portrait and messages hidden (bars, hud): the words as a caption
      if (!text.empty() && PortMotionValue(MOTION_HUD_ALPHA) < 0.5f) {
        Caption c;
        c.text = text;
        c.t0 = Now();
        c.fLength = s;
        c.nAt = 0;
        g_captions.push_back(c);
      }
      return s;
    }
    case K_SOUND: {
      PortTrigSound(j["path"].Str("").c_str());
      return 0.0;
    }
    case K_WAIT:
      return Seconds(j, 1.0);
  }
  return 0.0;
}

// The end: the picture back as the game draws it and the zoom as it was, in half a second;
// the cutscene plays on until then, so the camera is let go where it stands.
void BeginEnd(void)
{
  if (g_bEnding)
    return;
  g_bEnding = true;
  const double s = 0.5;
  g_fEndAt = Now() + s;
  if (g_bRest) {
    PortMotionTo(MOTION_BLACK, 0.0f, s, 0.0, MOTION_EASE);
    PortMotionTo(MOTION_BARS, 0.0f, s, 0.0, MOTION_EASE);
    PortMotionTo(MOTION_MAP_SCALE, 1.0f, s, 0.0, MOTION_IN_OUT);
    PortMotionTo(MOTION_HUD_ALPHA, 1.0f, s, 0.0, MOTION_EASE);
    PortMotionTo(MOTION_HUD_SHIFT, 0.0f, s, 0.0, MOTION_EASE);
  }
  if (g_cam.bOn || PortCameraZoom() != g_fZoomBefore) {
    GlideStart(s, MOTION_IN_OUT);
    g_cam.fToZ = g_fZoomBefore;
    g_cam.bZoom = true;
  }
  g_nNext = g_steps.size();
}

// The game's cursor (its layer 0, cur.cpp) off while the player's input is held, so it neither
// shows over the bars nor fades with the console; back as it was at the end.
void CursorHold(bool bHold)
{
  if (bHold && _img_list[0].bInUse) {
    cur_hide();
    g_bCursorHidden = true;
  } else if (!bHold && g_bCursorHidden) {
    g_bCursorHidden = false;
    cur_show();
  }
}

void Reset(void)
{
  CursorHold(false);
  g_bPlaying = g_bEnding = false;
  g_steps.clear();
  g_nNext = 0;
  g_captions.clear();
  g_labels.clear();
  g_cam.bOn = false;
}

const char *KindName(int nKind)
{
  return nKind >= 0 && nKind < K_KINDS ? kKinds[nKind] : "?";
}

}  // namespace

bool PortCutscenePlay(const PortJson &scene, std::string *pError)
{
  if (gwGameMode != 3) {
    *pError = "a cutscene needs a game";
    return false;
  }
  const PortJson &steps = scene["steps"];
  if (steps.kind != PortJson::ARRAY || steps.items.empty()) {
    *pError = "cutscene_play needs steps, a list of {do: ...}";
    return false;
  }
  std::vector<Step> parsed;
  std::map<std::string, bool> labels;
  for (size_t i = 0; i < steps.items.size(); ++i) {
    const PortJson &j = steps.items[i];
    const std::string what = j["do"].Str("");
    int nKind = -1;
    for (int k = 0; k < K_KINDS; ++k)
      if (what == kKinds[k])
        nKind = k;
    char szAt[32];
    snprintf(szAt, sizeof szAt, "step %u: ", static_cast<unsigned>(i));
    if (nKind < 0) {
      *pError = std::string(szAt) + "do is one of camera, follow, spawn, order, kill, remove, fade, bars, hud, "
                                    "depart, arrive, caption, text, transmission, sound, wait";
      return false;
    }
    if (Acts(nKind) && !InSinglePlayer()) {
      *pError = std::string(szAt) + what + " acts on the game's units, which a cutscene does only in a single-player game";
      return false;
    }
    if (nKind == K_SPAWN) {
      if (!PortTrigCreateFits(UnitType(j["unit"]), static_cast<int>(j["x"].Num(-1)), static_cast<int>(j["y"].Num(-1)),
                              static_cast<int>(j["player"].Num(0)), static_cast<int>(j["count"].Num(1)))) {
        *pError = std::string(szAt) + "spawn needs unit (an id or a name), x and y on the map, a player of the game, count 1-64";
        return false;
      }
      if (j["as"].kind == PortJson::STRING)
        labels[j["as"].s] = true;
    }
    if (nKind == K_ORDER && j["to"].size() != 2) {
      *pError = std::string(szAt) + "order needs to, [x, y]";
      return false;
    }
    if ((nKind == K_ORDER || nKind == K_KILL || nKind == K_REMOVE || nKind == K_FOLLOW) &&
        (nKind == K_FOLLOW ? j["unit"] : j["units"]).kind == PortJson::NONE) {
      *pError = std::string(szAt) + what + (nKind == K_FOLLOW ? " needs unit" : " needs units") +
                ": a label a spawn gave (as), an id, or a list of them";
      return false;
    }
    const PortJson &named = nKind == K_FOLLOW || nKind == K_TRANSMISSION ? j["unit"] : j["units"];
    if (named.kind == PortJson::STRING && labels.find(named.s) == labels.end()) {
      *pError = std::string(szAt) + "no spawn before it is labelled " + named.s;
      return false;
    }
    if ((nKind == K_CAPTION || nKind == K_TEXT) && j["text"].Str("").empty()) {
      *pError = std::string(szAt) + what + " needs text";
      return false;
    }
    if (nKind == K_TRANSMISSION && j["portrait"].kind != PortJson::NONE && UnitType(j["portrait"]) < 0) {
      *pError = std::string(szAt) + "transmission's portrait is a unit's (an id or a name)";
      return false;
    }
    if (nKind == K_SOUND && j["path"].Str("").empty()) {
      *pError = std::string(szAt) + "sound needs path";
      return false;
    }
    Step st;
    st.nKind = nKind;
    st.j = j;
    st.bAsync = j["async"].kind == PortJson::BOOL && j["async"].b;
    parsed.push_back(st);
  }
  const double fZoomBefore = g_bPlaying ? g_fZoomBefore : PortCameraZoomTarget();
  Reset();
  g_steps.swap(parsed);
  g_bPlaying = true;
  g_bHold = scene["hold"].kind != PortJson::BOOL || scene["hold"].b;
  g_bSkip = scene["skip"].kind != PortJson::BOOL || scene["skip"].b;
  g_bRest = scene["rest"].kind != PortJson::BOOL || scene["rest"].b;
  g_fZoomBefore = fZoomBefore;
  g_fStepDone = Now();
  PortPresentRedraw();
  return true;
}

void PortCutsceneStop(void)
{
  if (g_bPlaying)
    BeginEnd();
}

bool PortCutsceneRunning(void)
{
  return g_bPlaying;
}

std::string PortCutsceneStatusJson(void)
{
  std::string s = "\"playing\":" + std::string(g_bPlaying ? "true" : "false");
  if (!g_bPlaying)
    return s;
  const size_t nAt = g_nNext > 0 ? g_nNext - 1 : 0;
  s += ",\"step\":" + std::to_string(nAt) + ",\"steps\":" + std::to_string(g_steps.size());
  s += ",\"do\":\"" + std::string(nAt < g_steps.size() ? KindName(g_steps[nAt].nKind) : "end") + "\"";
  s += ",\"ending\":" + std::string(g_bEnding ? "true" : "false");
  s += ",\"units\":{";
  bool bFirst = true;
  for (const auto &label : g_labels) {
    s += (bFirst ? "\"" : ",\"") + label.first + "\":[";
    for (size_t i = 0; i < label.second.size(); ++i)
      s += (i ? "," : "") + std::to_string(label.second[i]);
    s += "]";
    bFirst = false;
  }
  return s + "}";
}

void PortCutsceneTick(void)
{
  if (!g_bPlaying)
    return;
  if (gwGameMode != 3) {
    g_bCursorHidden = false;  // the game's layers go with it
    Reset();
    return;
  }
  CursorHold(g_bHold && !g_bEnding);
  const double t = Now();
  while (!g_bEnding && t >= g_fStepDone) {
    if (g_nNext >= g_steps.size()) {
      BeginEnd();
      break;
    }
    const Step &st = g_steps[g_nNext++];
    const double s = Run(st);
    if (!st.bAsync)
      g_fStepDone = t + s;
  }
  // the zoom, eased in equal ratios
  if (g_cam.bOn && g_cam.bZoom) {
    const double u = g_cam.fLength > 0.0 ? (t - g_cam.t0) / g_cam.fLength : 1.0;
    const double e = Eased(g_cam.nEase, u);
    const double fFrom = g_cam.fFromZ > 0.0 ? g_cam.fFromZ : 1.0, fTo = g_cam.fToZ > 0.0 ? g_cam.fToZ : 1.0;
    PortCameraZoomSet(exp(log(fFrom) + (log(fTo) - log(fFrom)) * e));
  }
  for (size_t i = 0; i < g_captions.size();)
    if (t > g_captions[i].t0 + g_captions[i].fLength)
      g_captions.erase(g_captions.begin() + static_cast<LONG>(i));
    else
      ++i;
  if (g_bEnding && t >= g_fEndAt) {
    Reset();
    PortPresentRedraw();
    return;
  }
  PortPresentRedraw();
}

bool PortCutsceneAim(double *pfX, double *pfY)
{
  if (!g_bPlaying || !g_cam.bOn || !g_cam.bPlace)
    return false;
  double tx = g_cam.fToX, ty = g_cam.fToY;
  if (g_cam.nUnit >= 0) {
    const CUnit *pUnit = PortTrigUnit(g_cam.nUnit);
    if (pUnit != NULL && pUnit->pSprite != NULL) {
      tx = g_cam.fToX = pUnit->pSprite->wPosX;
      ty = g_cam.fToY = static_cast<short>(pUnit->pSprite->wPosY);
    }
  }
  const double u = g_cam.fLength > 0.0 ? (Now() - g_cam.t0) / g_cam.fLength : 1.0;
  const double e = Eased(g_cam.nEase, u);
  *pfX = g_cam.fFromX + (tx - g_cam.fFromX) * e;
  *pfY = g_cam.fFromY + (ty - g_cam.fFromY) * e;
  // inside the map
  const double fW = gwMapWidth * 32.0, fH = gwMapHeight * 32.0;
  *pfX = *pfX < 0.0 ? 0.0 : *pfX > fW ? fW : *pfX;
  *pfY = *pfY < 0.0 ? 0.0 : *pfY > fH ? fH : *pfY;
  return true;
}

// Whether a caption shows.
static bool LayerShown(void)
{
  return g_bPlaying && !g_captions.empty();
}

// The captions, in the box of the game's picture.
static bool LayerDraw(int nWidth, int nHeight, float fX, float fY, float fW, float fH)
{
  (void)nWidth;
  (void)nHeight;
  if (!LayerShown())
    return false;
  const double t = Now();
  const float fSize = SDL_clamp(fH * 0.034f, 14.0f, 44.0f);
  const float fBars = PortMotionValue(MOTION_BARS);
  const float fBar = fH * SDL_min(fBars, 0.5f);
  for (const Caption &c : g_captions) {
    // in a quarter second, out in the last third of one
    const double fIn = (t - c.t0) / 0.25, fOut = (c.t0 + c.fLength - t) / 0.35;
    const float fAlpha = static_cast<float>(SDL_clamp(SDL_min(fIn, fOut), 0.0, 1.0));
    // the lines: the text's own, and wrapped to most of the picture's width
    std::vector<std::string> lines;
    const float fMax = fW * 0.8f;
    size_t nStart = 0;
    while (nStart <= c.text.size()) {
      size_t nEnd = c.text.find('\n', nStart);
      if (nEnd == std::string::npos)
        nEnd = c.text.size();
      std::string para = c.text.substr(nStart, nEnd - nStart), line;
      size_t p = 0;
      while (p <= para.size()) {
        size_t q = para.find(' ', p);
        if (q == std::string::npos)
          q = para.size();
        const std::string word = para.substr(p, q - p);
        const std::string tried = line.empty() ? word : line + " " + word;
        if (!line.empty() && UiTextWidth(UI_FACE_TEXT, fSize, 0.0f, tried.c_str()) > fMax) {
          lines.push_back(line);
          line = word;
        } else {
          line = tried;
        }
        p = q + 1;
      }
      lines.push_back(line);
      nStart = nEnd + 1;
    }
    const float fLine = fSize * 1.3f;
    const float fBlock = fLine * static_cast<float>(lines.size());
    // in the bar when the bars are in, else over the picture
    float fTop;
    if (c.nAt == 2)
      fTop = fBar > fBlock ? fY + (fBar - fBlock) * 0.5f : fY + fH * 0.1f;
    else if (c.nAt == 1)
      fTop = fY + (fH - fBlock) * 0.5f;
    else
      fTop = fBar > fBlock ? fY + fH - fBar + (fBar - fBlock) * 0.5f : fY + fH * 0.66f - fBlock;
    for (size_t i = 0; i < lines.size(); ++i) {
      const float w = UiTextWidth(UI_FACE_TEXT, fSize, 0.0f, lines[i].c_str());
      const float x = fX + (fW - w) * 0.5f, fBase = fTop + fLine * static_cast<float>(i) + fSize;
      UiText(x + 1.5f, fBase + 1.5f, UI_FACE_TEXT, fSize, 0.0f, UiRgb(0x000000, 0.8f * fAlpha), lines[i].c_str());
      UiText(x, fBase, UI_FACE_TEXT, fSize, 0.0f, UiRgb(0xF2F0E6, fAlpha), lines[i].c_str());
    }
  }
  return true;
}

// The player's keys and buttons held back while one plays; Escape ends one that may be skipped.
static bool LayerHold(unsigned uMsg, unsigned long long wParam, long long lParam)
{
  if (!g_bPlaying || g_bEnding || !g_bHold)
    return false;
  if (uMsg == 0x100 && wParam == 0x1B && g_bSkip) {  // WM_KEYDOWN, Escape
    BeginEnd();
    return true;
  }
  return PortMotionIsAction(uMsg, lParam);
}

// The words and the held input as a layer over the game (port_overlay.h).
const PortOverlayLayer kLayer = {
    "cutscene", PORT_OVERLAY_CUTSCENE, 0,
    LayerShown, NULL, LayerDraw, LayerHold, NULL, NULL, NULL};
PORT_OVERLAY_LAYER(kLayer);
