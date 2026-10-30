// port_world.cpp -- the world behind the front end (docs/front-end.md, F1). Presentation only.
//
// Each world the campaigns are fought on is drawn from what the lore says it looks like, by
// the planet shader (shaders/ui.hlsl, PSPlanet), out of its row in the world table. It uses
// no art at all. The design's own worlds.js holds the table: tools/gen_worlds.py
// writes port_world_table.h from it, and the camera's views here are its own.
//
// The camera eases from view to view as the screens change, a slow drift keeps the scene
// alive, the planet turns about a tilted axis, and a change of world fades through black so
// the swap is never seen.
#include <math.h>
#include <string.h>
#include "port_world.h"
#include "port_ui.h"  // UiEase, UiSpan
#include "port_text.h"  // PT_, in the table

namespace {

struct Moon {
  float at[3];
  float r;        // 0: none
  float col[3];
  float craters;
  bool bGas;      // a gas giant, in bands
};

// A world's look; the fields are worlds.js's, which says what each does, in the order
// tools/gen_worlds.py writes them.
struct World {
  const char *pszSlug;
  const char *pszName;
  const char *pszArea;
  const char *pszNote;
  const char *pszState;
  float sea;
  float deep[3], shallow[3];
  float land[4][3];
  float alt[2][3];
  float moist;
  float ice;
  float iceCol[3];
  float seed, scale, relief, dunes, craters;
  float tilt;         // degrees
  float city;
  float cityAt[3];
  float citySpread;   // 0: the city is everywhere it can be
  float cityDay[3], cityCol[3];
  float cityGain;
  float scar;
  float scarCol[3];
  float glass;
  float creep;
  float creepCol[3], creepGlow[3];
  float lava;
  float lavaCol[3];
  float cloud;
  float cloudCol[3];
  float bands, cloudSpeed;
  float haze[4];
  float atmo[3];
  float atmoThick;
  float star[3];
  float starGain;
  float star2[3];     // a second sun's colour, its strength (0: none), and its angle from the first
  float star2Gain, star2Angle;
  float neb[2][3];
  int nMoons;
  Moon moons[3];
};

const World kWorlds[] = {
#include "port_world_table.h"
};

struct View {
  const char *pszName;
  float pos[3], at[3];
  float roll, fov;
  float sun[3];
  float dim;
};

const View kViews[] = {
  // over the limb at sunrise: the night side and its lights below, the sun breaking the edge
  {"home", {0.3f, 0.3f, 1.95f}, {0.12f, 1.0f, 0.0f}, -0.1f, 52.0f, {0.6f, 0.24f, -1.0f}, 1.0f},
  // the planet on the right, half lit, for the panel on the left
  {"campaign", {-1.35f, 0.3f, 2.9f}, {-1.2f, 0.1f, 0.0f}, 0.0f, 44.0f, {-1.0f, 0.3f, 0.15f}, 0.9f},
  // further out, the planet low on the left
  {"mods", {1.2f, 0.9f, 3.8f}, {1.35f, 0.55f, 0.0f}, 0.06f, 42.0f, {-0.55f, 0.45f, 0.55f}, 0.85f},
  // close over the terminator, dimmed under the settings panel
  {"settings", {0.0f, -0.25f, 1.9f}, {0.0f, 0.9f, 0.0f}, 0.05f, 56.0f, {-0.9f, 0.5f, -0.4f}, 0.6f},
};

const float kSpinRate = 0.005f;  // radians a second: a turn in about 21 minutes

struct Camera {
  float pos[3], at[3];
  float roll, fov;
  float sun[3];
  float dim;
};

const World *World0(void)
{
  for (const World &w : kWorlds)
    if (strcmp(w.pszSlug, "korhal") == 0)
      return &w;
  return &kWorlds[0];
}

const World *g_pWorld = World0();
const World *g_pPending;
Camera g_cur;
Camera g_goal;
bool g_bCamera;
float g_fade = 1.0f;
float g_fadeGoal = 1.0f;
float g_fIntroAt = -1.0f;  // seconds into the intro, below 0 when none runs
float g_fIntro = 1.0f;     // how far out of black it has come
double g_dTime;

float Dot(const float *a, const float *b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }

void Norm(float *v)
{
  float l = sqrtf(Dot(v, v));
  if (l > 0.0f) {
    v[0] /= l;
    v[1] /= l;
    v[2] /= l;
  }
}

void Cross(const float *a, const float *b, float *o)
{
  o[0] = a[1] * b[2] - a[2] * b[1];
  o[1] = a[2] * b[0] - a[0] * b[2];
  o[2] = a[0] * b[1] - a[1] * b[0];
}

void Set3(float *o, const float *v) { o[0] = v[0], o[1] = v[1], o[2] = v[2]; }
void Set4(float *o, const float *v, float w) { o[0] = v[0], o[1] = v[1], o[2] = v[2], o[3] = w; }

Camera CameraOf(const char *pszView)
{
  const View *v = &kViews[0];
  for (const View &x : kViews)
    if (pszView != NULL && strcmp(x.pszName, pszView) == 0)
      v = &x;
  Camera c;
  Set3(c.pos, v->pos);
  Set3(c.at, v->at);
  c.roll = v->roll;
  c.fov = v->fov;
  Set3(c.sun, v->sun);
  Norm(c.sun);
  c.dim = v->dim;
  return c;
}

// The camera as WorldParams uses it this frame.
struct Frame {
  float pos[3], fwd[3], right[3], up[3];
  float tanHalf;
};

Frame FrameNow(void)
{
  Frame f;
  const float t = static_cast<float>(g_dTime);
  f.pos[0] = g_cur.pos[0] + sinf(t * 0.05f) * 0.03f;
  f.pos[1] = g_cur.pos[1] + sinf(t * 0.037f) * 0.015f;
  f.pos[2] = g_cur.pos[2];
  float fwd[3] = {g_cur.at[0] - f.pos[0], g_cur.at[1] - f.pos[1], g_cur.at[2] - f.pos[2]};
  Norm(fwd);
  const float yAxis[3] = {0, 1, 0};
  float right[3];
  float up[3];
  Cross(fwd, yAxis, right);
  Norm(right);
  Cross(right, fwd, up);
  const float cr = cosf(g_cur.roll);
  const float sr = sinf(g_cur.roll);
  for (int i = 0; i < 3; ++i) {
    f.fwd[i] = fwd[i];
    f.right[i] = right[i] * cr + up[i] * sr;
    f.up[i] = up[i] * cr - right[i] * sr;
  }
  f.tanHalf = tanf(g_cur.fov * 3.14159265f / 360.0f);
  return f;
}

}  // namespace

void WorldSetPlanet(const char *pszSlug, bool bInstant)
{
  const World *p = World0();
  for (const World &w : kWorlds)
    if (pszSlug != NULL && strcmp(w.pszSlug, pszSlug) == 0)
      p = &w;
  if (p == g_pWorld && g_pPending == NULL)
    return;
  if (bInstant) {
    g_pWorld = p;
    g_pPending = NULL;
    g_fade = g_fadeGoal = 1.0f;
    return;
  }
  g_pPending = p;
  g_fadeGoal = 0.0f;
}

const char *WorldPlanet(void)
{
  return (g_pPending != NULL ? g_pPending : g_pWorld)->pszSlug;
}

void WorldAboutNow(WorldAbout *pAbout)
{
  const World &w = *(g_pPending != NULL ? g_pPending : g_pWorld);
  pAbout->pszName = w.pszName;
  pAbout->pszArea = w.pszArea;
  pAbout->pszNote = w.pszNote;
  pAbout->pszState = w.pszState;
}

void WorldIntroBegin(void)
{
  g_fIntroAt = 0.0f;
  g_fIntro = 0.0f;
}

void WorldIntroSkip(void)
{
  g_fIntroAt = -1.0f;
  g_fIntro = 1.0f;
}

void WorldSetView(const char *pszView, bool bInstant)
{
  g_goal = CameraOf(pszView);
  if (bInstant || !g_bCamera) {
    g_cur = g_goal;
    g_bCamera = true;
  }
}

void WorldStep(double dSeconds)
{
  if (!g_bCamera)
    WorldSetView("home", true);
  const float dt = static_cast<float>(dSeconds > 0.1 ? 0.1 : dSeconds);
  g_dTime += dSeconds;
  g_fade += (g_fadeGoal - g_fade) * (dt * 9.0f > 1.0f ? 1.0f : dt * 9.0f);
  if (g_pPending != NULL && g_fade < 0.03f) {
    g_pWorld = g_pPending;
    g_pPending = NULL;
    g_fadeGoal = 1.0f;
  }
  if (g_fIntroAt >= 0.0f) {
    g_fIntroAt += static_cast<float>(dSeconds);
    g_fIntro = UiEase(UiSpan(g_fIntroAt, 0.0f, 1.1f));
    if (g_fIntroAt >= 1.1f)
      g_fIntroAt = -1.0f;
  }
  const float e = 1.0f - expf(-dt * 1.8f);
  for (int i = 0; i < 3; ++i) {
    g_cur.pos[i] += (g_goal.pos[i] - g_cur.pos[i]) * e;
    g_cur.at[i] += (g_goal.at[i] - g_cur.at[i]) * e;
    g_cur.sun[i] += (g_goal.sun[i] - g_cur.sun[i]) * e;
  }
  Norm(g_cur.sun);
  g_cur.roll += (g_goal.roll - g_cur.roll) * e;
  g_cur.fov += (g_goal.fov - g_cur.fov) * e;
  g_cur.dim += (g_goal.dim - g_cur.dim) * e;
}

// How much of a sun the planet hides from the camera: 0 behind it, 1 clear of its edge.
static float SunShows(const float *pos, const float *dir)
{
  const float bb = Dot(pos, dir);
  const float dmin = sqrtf(fmaxf(0.0f, Dot(pos, pos) - bb * bb));
  return bb < 0.0f ? fminf(1.0f, fmaxf(0.0f, (dmin - 0.985f) / 0.05f)) : 1.0f;
}

void WorldParams(int nWidth, int nHeight, PlanetParams *p)
{
  const World &w = *g_pWorld;
  const Frame f = FrameNow();
  const float t = static_cast<float>(g_dTime);
  const float fw = static_cast<float>(nWidth);
  const float fh = static_cast<float>(nHeight);
  memset(p, 0, sizeof *p);

  // the sun on the target, and how much of it the planet hides
  float sunScreen[2] = {-1e5f, -1e5f};
  float vis = 0.0f;
  const float sx = Dot(g_cur.sun, f.right);
  const float sy = Dot(g_cur.sun, f.up);
  const float sz = Dot(g_cur.sun, f.fwd);
  if (sz > 0.01f) {
    const float aspect = fw / fh;
    sunScreen[0] = ((sx / (sz * f.tanHalf)) / aspect + 1.0f) / 2.0f * fw;
    sunScreen[1] = ((sy / (sz * f.tanHalf)) + 1.0f) / 2.0f * fh;
    vis = SunShows(f.pos, g_cur.sun);
  }
  // a second sun, the world's angle round from the first and a little above it
  float sun2[3] = {0.0f, 1.0f, 0.0f};
  float vis2 = 0.0f;
  if (w.star2Gain > 0.0f) {
    const float ca2 = cosf(w.star2Angle);
    const float sa2 = sinf(w.star2Angle);
    sun2[0] = g_cur.sun[0] * ca2 + g_cur.sun[2] * sa2;
    sun2[1] = g_cur.sun[1] + 0.25f;
    sun2[2] = -g_cur.sun[0] * sa2 + g_cur.sun[2] * ca2;
    Norm(sun2);
    vis2 = SunShows(f.pos, sun2);
  }
  // the planet turns about y, its axis tilted about z as the lore has it
  const float a = t * kSpinRate + 0.6f;
  const float tilt = w.tilt * 3.14159265f / 180.0f;
  const float ca = cosf(a), sa = sinf(a), ct = cosf(tilt), st = sinf(tilt);
  const float spin[3][3] = {{ca * ct, -st, sa * ct}, {ca * st, ct, sa * st}, {-sa, 0.0f, ca}};
  const float cloudShift = t * kSpinRate * w.cloudSpeed / (2.0f * 3.14159265f);

  p->res[0] = fw;
  p->res[1] = fh;
  p->res[2] = t;
  p->res[3] = g_fade * g_fIntro;
  Set4(p->camPos, f.pos, f.tanHalf);
  Set4(p->camFwd, f.fwd, cloudShift);
  Set4(p->camRight, f.right, vis);
  Set4(p->camUp, f.up, g_cur.dim);
  Set4(p->sun, g_cur.sun, vis2);
  Set4(p->sun2, sun2, w.glass);
  const float star2[3] = {w.star2[0] * w.star2Gain, w.star2[1] * w.star2Gain, w.star2[2] * w.star2Gain};
  Set4(p->star2, star2, w.cityGain);
  for (int i = 0; i < 3; ++i)
    Set4(p->spin[i], spin[i], 0.0f);
  p->sunScreen[0] = sunScreen[0];
  p->sunScreen[1] = sunScreen[1];
  p->sunScreen[2] = w.scale;
  p->seedV[0] = fmodf(w.seed * 1.73f, 17.0f);
  p->seedV[1] = fmodf(w.seed * 3.11f, 19.0f);
  p->seedV[2] = fmodf(w.seed * 2.37f, 13.0f);
  p->sea[0] = w.sea;
  p->sea[1] = w.moist;
  p->sea[2] = w.ice;
  p->sea[3] = w.dunes;
  Set4(p->deep, w.deep, 0.0f);
  Set4(p->shallow, w.shallow, 0.0f);
  for (int i = 0; i < 4; ++i)
    Set4(p->land[i], w.land[i], 0.0f);
  for (int i = 0; i < 2; ++i)
    Set4(p->alt[i], w.alt[i], 0.0f);
  Set4(p->iceCol, w.iceCol, w.craters);
  p->relief[0] = w.relief;
  p->relief[1] = w.city;
  p->relief[2] = w.scar;
  p->relief[3] = w.creep;
  float at[3] = {w.cityAt[0], w.cityAt[1], w.cityAt[2]};
  Norm(at);
  Set4(p->cityAt, at, w.citySpread);
  Set4(p->cityCol, w.cityCol, 0.0f);
  Set4(p->cityDay, w.cityDay, 0.0f);
  Set4(p->scarCol, w.scarCol, 0.0f);
  Set4(p->creepCol, w.creepCol, 0.0f);
  Set4(p->creepGlow, w.creepGlow, 0.0f);
  Set4(p->lavaCol, w.lavaCol, w.lava);
  p->cloud[0] = w.cloud;
  p->cloud[1] = w.bands;
  p->cloud[2] = w.atmoThick;
  p->cloud[3] = w.starGain;
  Set4(p->cloudCol, w.cloudCol, 0.0f);
  memcpy(p->haze, w.haze, sizeof p->haze);
  Set4(p->atmo, w.atmo, 0.0f);
  Set4(p->star, w.star, 0.0f);
  Set4(p->nebA, w.neb[0], 0.0f);
  Set4(p->nebB, w.neb[1], 0.0f);
  for (int i = 0; i < w.nMoons; ++i) {
    Set4(p->moon[i], w.moons[i].at, w.moons[i].r);
    Set4(p->moonCol[i], w.moons[i].col, w.moons[i].craters);
    p->moonGas[i] = w.moons[i].bGas ? 1.0f : 0.0f;
  }
}

