// port_intro.cpp -- the start of a skirmish (docs/modules/front-end.md): the game
// held before its first frame while the map comes out of the front end's black and StarCraft
// II's skirmish countdown plays over it, to the design's timeline (countdown.js,
// port_intro_motion.h), with 1.17's countdown beep as each digit comes up. The game goes when
// the 1 grows away, the countdown's frame 95: nobody acts during it (the user's decision of
// 2026-10-14). The skirmish screen's games have it and a LAN room's match on every seat;
// missions, replays, loads and games the command line or 1.17's screens start do not.
//
// Presentation only. No frame runs while it holds (RunGameLoop asks PortIntroHolds before it
// steps), the player's clicks and keys wait (its layer's Hold, port_overlay.h), and the
// seconds it held are given back to the game's clock as 1.17's unpause gives them, so the
// score screen's time is the game's. A replay and the simulation see a start at frame 0.
#include <windows.h>
#include <math.h>
#include <vector>
#include <SDL3/SDL.h>
#include "port_front.h"  // PortFrontSkirmishGame
#include "../lan/port_lanlobby.h"  // PortLanLobbyActive
#include "port_intro.h"
#include "../render/port_present.h"  // PortPresentRedraw
#include "port_frontscreens.h"  // FrontLog
#include "port_ui.h"
#include "port_intro_motion.h"
#include "../render/port_overlay.h"  // PORT_OVERLAY_LAYER
#include "../render/port_motion.h"
#include "../hud/port_hudstate.h"  // PortHudArrive
#include "../gameplay/port_watchgame.h"  // PortWatchMaking
#include "glues.h"     // sub_49E740
#include "pause.h"     // sub_47A620
#include "saveload.h"  // IsALoadedGame
#include "../../gen/globals117.h"

namespace {

enum { INTRO_OFF, INTRO_ARMED, INTRO_PLAYING };
int g_nState;
bool g_bHolding;    // no frame runs
Uint64 g_nStartNs;  // when RunGameLoop first asked
double g_fStartAt;  // the motion's clock when it started playing (Since)
bool g_bArt;        // the pictures are in their slots
int g_nBeeped;      // the digits beeped for so far

// the map out of black and the console's rise are the motion's (PortMotionArrive)
const double kCountdownAt = 1.7;  // the countdown after the console's rise (front.js game)
const double kConsoleAt = 1.0;    // the console's rise, the HUD design's intro (PortMotionArrive)
const double kFps = 30.0;
const double kGoFrame = 95.0;     // the 1 grows away (countdown.js GO_FRAME)
const int kSfxCountdown = 0xAA;   // sfxdata's glue\countdown.wav, which 1.17's chat room beeps each second

enum { SLOT_BEAM = 16, SLOT_ORB, SLOT_HEX, SLOT_DIGIT3, SLOT_DIGIT2, SLOT_DIGIT1 };

// The beam's, the orb's and the hex field's sizes on the stage (countdown.js pieces).
const float kBeamW = 1670.0f, kBeamH = 424.0f;
const float kOrbW = 640.0f, kOrbH = 507.0f;
const float kHexW = 1187.0f, kHexH = 764.0f;
const float kDigitTall = 138.0f;  // a digit's height on the stage at full size

// The seconds into the start, on the motion's clock (port_motion.cpp), which RunGameLoop's
// passes move on.
double Since(void)
{
  return PortMotionClock() - g_fStartAt;
}

// A layer's channel at a frame, between its keys.
float Channel(const IntroLayer &L, int c, float f)
{
  const IntroKey *k = &kIntroKeys[L.anChannel[c][0]];
  const int n = L.anChannel[c][1];
  if (f <= k[0].nFrame)
    return k[0].fValue;
  for (int i = 1; i < n; ++i)
    if (f <= k[i].nFrame) {
      const float t = (f - k[i - 1].nFrame) / static_cast<float>(k[i].nFrame - k[i - 1].nFrame);
      return k[i - 1].fValue + (k[i].fValue - k[i - 1].fValue) * t;
    }
  return k[n - 1].fValue;
}

// ---------------------------------------------------------------------------
// The pictures, made once: each as countdown.js draws it on a canvas, at the picture
// atlas's size.

struct Stop {
  float at;
  float r, g, b, a;  // 0 to 255, and 0 to 1
};

void StopsAt(const Stop *s, int n, float t, float out[4])
{
  if (t <= s[0].at) {
    out[0] = s[0].r, out[1] = s[0].g, out[2] = s[0].b, out[3] = s[0].a;
    return;
  }
  for (int i = 1; i < n; ++i)
    if (t <= s[i].at) {
      const float u = (t - s[i - 1].at) / (s[i].at - s[i - 1].at);
      out[0] = s[i - 1].r + (s[i].r - s[i - 1].r) * u;
      out[1] = s[i - 1].g + (s[i].g - s[i - 1].g) * u;
      out[2] = s[i - 1].b + (s[i].b - s[i - 1].b) * u;
      out[3] = s[i - 1].a + (s[i].a - s[i - 1].a) * u;
      return;
    }
  out[0] = s[n - 1].r, out[1] = s[n - 1].g, out[2] = s[n - 1].b, out[3] = s[n - 1].a;
}

// src over dst, both straight colour
void Over(float d[4], const float s[4])
{
  const float a = s[3] + d[3] * (1.0f - s[3]);
  if (a <= 0.0f) {
    d[0] = s[0], d[1] = s[1], d[2] = s[2], d[3] = 0.0f;
    return;
  }
  for (int i = 0; i < 3; ++i)
    d[i] = (s[i] * s[3] + d[i] * d[3] * (1.0f - s[3])) / a;
  d[3] = a;
}

void Store(std::vector<unsigned char> &px, size_t i, const float c[4])
{
  for (int k = 0; k < 3; ++k)
    px[i * 4 + k] = static_cast<unsigned char>(fminf(255.0f, fmaxf(0.0f, c[k])) + 0.5f);
  px[i * 4 + 3] = static_cast<unsigned char>(fminf(1.0f, fmaxf(0.0f, c[3])) * 255.0f + 0.5f);
}

// The beam: a cone of blue light both ways from the middle, brightest at its centre.
void MakeBeam(void)
{
  static const Stop kWide[] = {{0.0f, 72, 148, 224, 0.22f}, {0.25f, 56, 116, 204, 0.14f},
                               {0.6f, 36, 72, 164, 0.06f}, {1.0f, 20, 40, 120, 0.0f}};
  static const Stop kCore[] = {{0.0f, 130, 196, 255, 0.2f}, {1.0f, 130, 196, 255, 0.0f}};
  const int w = UI_PICTURE_SIDE, h = static_cast<int>(w * kBeamH / kBeamW + 0.5f);
  std::vector<unsigned char> px(static_cast<size_t>(w) * h * 4);
  for (int j = 0; j < h; ++j)
    for (int i = 0; i < w; ++i) {
      const float x = (i + 0.5f) / w * kBeamW - kBeamW / 2, y = (j + 0.5f) / h * kBeamH - kBeamH / 2;
      float c[4], s[4];
      StopsAt(kWide, 4, sqrtf(x * x + (y / 0.254f) * (y / 0.254f)) / 835.0f, c);
      if (fabsf(y / 0.1f) <= 640.0f) {
        StopsAt(kCore, 2, sqrtf(x * x + (y / 0.1f) * (y / 0.1f)) / 640.0f, s);
        Over(c, s);
      }
      Store(px, static_cast<size_t>(j) * w + i, c);
    }
  UiPictureSet(SLOT_BEAM, px.data(), w, h);
}

// The orb: a deep blue sphere in a wide blue glow.
void MakeOrb(void)
{
  static const Stop kGlow[] = {{0.0f, 44, 112, 232, 0.7f}, {0.42f, 32, 84, 206, 0.36f}, {1.0f, 20, 40, 160, 0.0f}};
  static const Stop kBall[] = {{0.0f, 0x17, 0x4f, 0x9c, 0.92f}, {0.7f, 0x0f, 0x36, 0x70, 0.92f},
                               {1.0f, 0x0a, 0x25, 0x52, 0.92f}};
  const int w = UI_PICTURE_SIDE, h = static_cast<int>(w * kOrbH / kOrbW + 0.5f);
  const float fPerPx = kOrbW / w;
  std::vector<unsigned char> px(static_cast<size_t>(w) * h * 4);
  for (int j = 0; j < h; ++j)
    for (int i = 0; i < w; ++i) {
      const float x = (i + 0.5f) * fPerPx, y = (j + 0.5f) / h * kOrbH;
      float c[4], s[4];
      const float dy = (y - 253.5f) * 320.0f / 253.5f;
      StopsAt(kGlow, 3, sqrtf((x - 320) * (x - 320) + dy * dy) / 320.0f, c);
      const float d = sqrtf((x - 320) * (x - 320) + (y - 253.5f) * (y - 253.5f));
      // the sphere, lit from a little below and right of its middle, with its edge
      const float fIn = fminf(1.0f, fmaxf(0.0f, (112.0f - d) / fPerPx + 0.5f));
      if (fIn > 0.0f) {
        const float fx = x - 330, fy = y - 268;
        StopsAt(kBall, 3, sqrtf(fx * fx + fy * fy) / 125.0f, s);
        s[3] *= fIn;
        Over(c, s);
      }
      const float fRim = fminf(1.0f, fmaxf(0.0f, 1.0f - (fabsf(d - 112.0f) - 1.0f) / fPerPx));
      if (fRim > 0.0f) {
        const float r[4] = {70, 150, 255, 0.35f * fRim};
        Over(c, r);
      }
      Store(px, static_cast<size_t>(j) * w + i, c);
    }
  UiPictureSet(SLOT_ORB, px.data(), w, h);
}

// The hex field's mask: nothing in the middle, a wide ring, fading out at its edge.
float HexMask(float x, float y)
{
  static const Stop kMask[] = {{0.0f, 0, 0, 0, 0.0f}, {0.3f, 0, 0, 0, 0.0f}, {0.48f, 0, 0, 0, 1.0f},
                               {0.72f, 0, 0, 0, 0.55f}, {1.0f, 0, 0, 0, 0.0f}};
  const float dx = x - kHexW / 2, dy = (y - kHexH / 2) * kHexW / kHexH;
  float c[4];
  StopsAt(kMask, 5, sqrtf(dx * dx + dy * dy) / (kHexW / 2), c);
  return c[3];
}

// The field's faint fill, under its cells' outlines (drawn as lines, which stay sharp).
void MakeHex(void)
{
  const int w = UI_PICTURE_SIDE, h = static_cast<int>(w * kHexH / kHexW + 0.5f);
  std::vector<unsigned char> px(static_cast<size_t>(w) * h * 4);
  for (int j = 0; j < h; ++j)
    for (int i = 0; i < w; ++i) {
      const float c[4] = {92, 108, 196, 0.1f * HexMask((i + 0.5f) / w * kHexW, (j + 0.5f) / h * kHexH)};
      Store(px, static_cast<size_t>(j) * w + i, c);
    }
  UiPictureSet(SLOT_HEX, px.data(), w, h);
}

float g_fDigitPerUnit;    // a digit picture's pixels to a stage unit
int g_digitSize[3][2];    // each digit picture's width and height in pixels

// A digit in the wide face, white, with its glow round it (the design's text-shadow).
void MakeDigit(int nSlot, char cDigit)
{
  const int nTall = 220, nPad = 48;
  std::vector<unsigned char> cover;
  int w = 0, h = 0;
  // the size whose 3 stands nTall pixels, as the design sizes its digits by the 3's height
  float fSize = 300.0f;
  if (UiGlyphCover(UI_FACE_WIDE, fSize, '3', &cover, &w, &h) && h > 0)
    fSize *= static_cast<float>(nTall) / h;
  if (!UiGlyphCover(UI_FACE_WIDE, fSize, static_cast<unsigned char>(cDigit), &cover, &w, &h))
    return;
  const int pw = SDL_min(w + 2 * nPad, static_cast<int>(UI_PICTURE_SIDE));
  const int ph = SDL_min(h + 2 * nPad, static_cast<int>(UI_PICTURE_SIDE));
  const int ox = (pw - w) / 2, oy = (ph - h) / 2;
  std::vector<float> a(static_cast<size_t>(pw) * ph, 0.0f), b(a.size());
  for (int j = 0; j < h; ++j)
    for (int i = 0; i < w; ++i)
      a[static_cast<size_t>(j + oy) * pw + i + ox] = cover[static_cast<size_t>(j) * w + i] / 255.0f;
  // the glow: three box blurs each way, about a gaussian
  std::vector<float> g = a;
  const int r = 9;
  for (int pass = 0; pass < 3; ++pass) {
    for (int j = 0; j < ph; ++j)
      for (int i = 0; i < pw; ++i) {
        float s = 0.0f;
        for (int k = -r; k <= r; ++k)
          s += g[static_cast<size_t>(j) * pw + SDL_clamp(i + k, 0, pw - 1)];
        b[static_cast<size_t>(j) * pw + i] = s / (2 * r + 1);
      }
    for (int j = 0; j < ph; ++j)
      for (int i = 0; i < pw; ++i) {
        float s = 0.0f;
        for (int k = -r; k <= r; ++k)
          s += b[static_cast<size_t>(SDL_clamp(j + k, 0, ph - 1)) * pw + i];
        g[static_cast<size_t>(j) * pw + i] = s / (2 * r + 1);
      }
  }
  std::vector<unsigned char> px(static_cast<size_t>(pw) * ph * 4);
  for (size_t i = 0; i < a.size(); ++i) {
    const float c[4] = {255, 255, 255, fmaxf(a[i], fminf(1.0f, g[i] * 1.3f))};
    Store(px, i, c);
  }
  UiPictureSet(nSlot, px.data(), pw, ph);
  g_digitSize[nSlot - SLOT_DIGIT3][0] = pw;
  g_digitSize[nSlot - SLOT_DIGIT3][1] = ph;
  g_fDigitPerUnit = nTall / kDigitTall;
}

void MakeArt(void)
{
  if (g_bArt)
    return;
  g_bArt = true;
  MakeBeam();
  MakeOrb();
  MakeHex();
  MakeDigit(SLOT_DIGIT3, '3');
  MakeDigit(SLOT_DIGIT2, '2');
  MakeDigit(SLOT_DIGIT1, '1');
}

// ---------------------------------------------------------------------------
// Drawing a moment of it, on a target nWidth by nHeight, the stage fitted to its height.

struct Stage {
  float k, ox;
  float X(float x) const { return ox + x * k; }
  float Y(float y) const { return y * k; }
};

void Picture(const Stage &st, int nSlot, float x, float y, float w, float h, UiColor c)
{
  UiPicture(st.X(x) - w * st.k / 2, st.Y(y) - h * st.k / 2, w * st.k, h * st.k, nSlot, c);
}

// The ring: three quarters of a circle, 132 units round, its tail dark, cyan, near white over
// the top and cyan again at the cut, turned by fTurn degrees; in short lines, which turn.
void Ring(const Stage &st, float x, float y, float s, float fTurn, float fAlpha)
{
  static const Stop kConic[] = {{0.0f, 10, 20, 40, 0.0f},       {0.12f, 18, 48, 86, 0.6f},
                                {0.3f, 22, 198, 214, 1.0f},     {0.62f, 222, 216, 232, 0.92f},
                                {0.9f, 64, 202, 216, 1.0f},     {1.0f, 34, 190, 204, 1.0f}};
  const float a0 = 100.0f, a1 = 360.0f;
  const int n = 72;
  const float cx = st.X(x), cy = st.Y(y), rad = 132.0f * s * st.k, wide = 9.0f * s * st.k;
  for (int pass = 0; pass < 2; ++pass)
    for (int i = 0; i < n; ++i) {
      const float t0 = a0 + (a1 - a0) * i / n, t1 = a0 + (a1 - a0) * (i + 1.08f) / n;
      float c[4];
      StopsAt(kConic, 6, ((t0 + t1) * 0.5f - a0) / 360.0f, c);
      const float r0 = (t0 + fTurn) * 3.14159265f / 180.0f, r1 = (SDL_min(t1, a1) + fTurn) * 3.14159265f / 180.0f;
      const float x0 = cx + rad * cosf(r0), y0 = cy + rad * sinf(r0);
      const float x1 = cx + rad * cosf(r1), y1 = cy + rad * sinf(r1);
      if (pass == 0)  // the shadow's glow under it
        UiLine(x0, y0, x1, y1, UiRgb(0x28c8dc, 0.16f * c[3] * fAlpha), wide + 10.0f * s * st.k);
      else
        UiLine(x0, y0, x1, y1, {c[0] / 255, c[1] / 255, c[2] / 255, c[3] * fAlpha}, wide);
    }
  UiSetTurn(0.0f, 0.0f, 0.0f);
}

// The hex field's cells: flat-topped, outlined dark where the mask lets them show.
void HexLines(const Stage &st, float x, float y, float s, float fAlpha)
{
  const float side = 34.0f, hw = side * 1.5f, hh = side * 1.7320508f;
  const float left = x - kHexW / 2 * s, top = y - kHexH / 2 * s;
  for (int col = -1; col * hw < kHexW + side; ++col)
    for (int row = -1; row * hh < kHexH + side; ++row) {
      const float cx = col * hw, cy = row * hh + (col % 2 != 0 ? hh / 2 : 0.0f);
      const float m = HexMask(cx, cy) * fAlpha;
      if (m < 0.02f)
        continue;
      for (int k = 0; k < 6; ++k) {
        const float a = k * 1.0471976f, b = (k + 1) * 1.0471976f;
        const float x0 = left + (cx + (side - 2) * cosf(a)) * s, y0 = top + (cy + (side - 2) * sinf(a)) * s;
        const float x1 = left + (cx + (side - 2) * cosf(b)) * s, y1 = top + (cy + (side - 2) * sinf(b)) * s;
        UiLine(st.X(x0), st.Y(y0), st.X(x1), st.Y(y1), UiRgb(0x0e1434, 0.5f * m), 2.2f * s * st.k);
      }
    }
  UiSetTurn(0.0f, 0.0f, 0.0f);
}

void Digit(const Stage &st, int nSlot, const IntroLayer &L, float f)
{
  const float s = Channel(L, INTRO_SX, f);
  const float fAlpha = Channel(L, INTRO_ALPHA, f);
  if (g_fDigitPerUnit <= 0.0f || fAlpha <= 0.002f || s <= 0.001f)
    return;
  const float mult = Channel(L, INTRO_MULT, f), add = Channel(L, INTRO_ADD, f);
  // the ink, #dbfcf9 times the multiplier with the addition
  const UiColor ink = {fminf(1.0f, fmaxf(0.0f, (0xdb * mult + add) / 255.0f)),
                       fminf(1.0f, fmaxf(0.0f, (0xfc * mult + add) / 255.0f)),
                       fminf(1.0f, fmaxf(0.0f, (0xf9 * mult + add) / 255.0f)), fminf(1.0f, fAlpha)};
  const float x = Channel(L, INTRO_X, f), y = Channel(L, INTRO_Y, f), blur = Channel(L, INTRO_BLUR, f);
  // the picture on the stage: the digit's ink centred where the design centres it
  const float w = g_digitSize[nSlot - SLOT_DIGIT3][0] * s / g_fDigitPerUnit;
  const float h = g_digitSize[nSlot - SLOT_DIGIT3][1] * s / g_fDigitPerUnit;
  if (blur > 0.5f) {
    // the horizontal blur as it leaves: copies smeared across the blur's width
    const int n = 9;
    UiColor c = ink;
    c.a = fminf(1.0f, fAlpha) / n * 1.6f;
    for (int i = 0; i < n; ++i)
      Picture(st, nSlot, x + (i / static_cast<float>(n - 1) - 0.5f) * blur, y, w, h, c);
  } else {
    Picture(st, nSlot, x, y, w, h, ink);
  }
}

// The countdown at frame f.
void Countdown(const Stage &st, int nWidth, int nHeight, float f)
{
  for (const IntroLayer &L : kIntroLayers) {
    if (f < L.nFirst || f > L.nLast)
      continue;
    const float fAlpha = fminf(1.0f, Channel(L, INTRO_ALPHA, f));
    if (fAlpha <= 0.002f)
      continue;
    if (L.nRole == INTRO_LIGHTBOX) {
      UiFill(0.0f, 0.0f, static_cast<float>(nWidth), static_cast<float>(nHeight), UiRgb(0x000000, fAlpha));
      continue;
    }
    const float x = Channel(L, INTRO_X, f), y = Channel(L, INTRO_Y, f);
    const float sx = Channel(L, INTRO_SX, f), sy = Channel(L, INTRO_SY, f);
    if (fabsf(sx) < 0.001f || fabsf(sy) < 0.001f)
      continue;
    const UiColor c = {1.0f, 1.0f, 1.0f, fAlpha};
    switch (L.nRole) {
      case INTRO_BEAM:
        Picture(st, SLOT_BEAM, x, y, kBeamW * sx, kBeamH * sy, c);
        break;
      case INTRO_ORB:
        Picture(st, SLOT_ORB, x, y, kOrbW * sx, kOrbH * sy, c);
        break;
      case INTRO_HEX:
        Picture(st, SLOT_HEX, x, y, kHexW * sx, kHexH * sy, c);
        HexLines(st, x, y, sx, fAlpha);
        break;
      case INTRO_RING:
        Ring(st, x, y, sx, Channel(L, INTRO_TURN, f), fAlpha);
        break;
      case INTRO_DIGIT3:
        Digit(st, SLOT_DIGIT3, L, f);
        break;
      case INTRO_DIGIT2:
        Digit(st, SLOT_DIGIT2, L, f);
        break;
      case INTRO_DIGIT1:
        Digit(st, SLOT_DIGIT1, L, f);
        break;
    }
  }
}

// The beep as each digit comes up, the 3, the 2 and the 1; once, for the last one reached, when
// a slow pass steps over more than one.
void Beep(double f)
{
  static const int kDigits[3] = {INTRO_DIGIT3, INTRO_DIGIT2, INTRO_DIGIT1};
  int nReached = 0;
  for (int i = 0; i < 3; ++i)
    for (const IntroLayer &L : kIntroLayers)
      if (L.nRole == kDigits[i] && f >= L.nFirst)
        nReached = i + 1;
  if (nReached > g_nBeeped) {
    g_nBeeped = nReached;
    sub_49E740(kSfxCountdown);
  }
}

}  // namespace

void PortIntroGameStarts(void)
{
  g_bHolding = false;
  g_nBeeped = 0;
  // the game a watched skirmish makes stops at its first frame (port_watchgame.h)
  const bool bSkirmish = PortFrontSkirmishGame() && !gbMultiPlayerMode && !PortWatchMaking();
  const bool bRoomMatch = PortLanLobbyActive() && gbMultiPlayerMode;
  g_nState = (bSkirmish || bRoomMatch) && gPlayback == 0 && !IsALoadedGame() ? INTRO_ARMED : INTRO_OFF;
  if (g_nState == INTRO_ARMED) {
    PortMotionArriveCancel();  // a room's match comes from under the lobby's cover (port_lanlobby.cpp)
    PortMotionArriveHold();
    FrontKit::FrontLog(bRoomMatch ? "intro: a room's match, held for its countdown\n"
                                  : "intro: a skirmish, held for its countdown\n");
  } else {
    PortMotionGameStarts();
  }
}

bool PortIntroArmed(void)
{
  return g_nState != INTRO_OFF;
}

bool PortIntroHolds(void)
{
  if (g_nState == INTRO_OFF)
    return false;
  if (g_nState == INTRO_ARMED) {
    g_nState = INTRO_PLAYING;
    g_nStartNs = SDL_GetTicksNS();
    g_fStartAt = PortMotionClock();
    g_bHolding = true;
    PortMotionArrive();
  }
  PortPresentRedraw();
  const double t = Since();
  const double f = (t - kCountdownAt) * kFps;
  if (t >= kConsoleAt)
    PortHudArrive();  // a HUD design comes in as the console would, before the countdown
  Beep(f);
  if (g_bHolding && f >= kGoFrame) {
    g_bHolding = false;
    // the held seconds back to the game's clock, as unpause_game(1) gives a pause's
    sub_47A620(static_cast<int>(static_cast<double>(SDL_GetTicksNS() - g_nStartNs) / 1e9 + 0.5));
    FrontKit::FrontLog("intro: the game goes after %.2f s\n", t);
  }
  if (f >= INTRO_FRAMES)
    g_nState = INTRO_OFF;
  return g_bHolding;
}

// The player's keys and buttons wait while it holds the game.
static bool LayerHold(unsigned uMsg, unsigned long long wParam, long long lParam)
{
  (void)wParam;
  if (g_nState == INTRO_OFF || (g_nState == INTRO_PLAYING && !g_bHolding))
    return false;
  return PortMotionIsAction(uMsg, lParam);
}

// Over all of the target, the game's box aside; false when there is nothing to draw.
static bool LayerDraw(int nWidth, int nHeight, float fX, float fY, float fW, float fH)
{
  (void)fX;
  (void)fY;
  (void)fW;
  (void)fH;
  if (g_nState == INTRO_OFF)
    return false;
  const float W = static_cast<float>(nWidth), H = static_cast<float>(nHeight);
  // the black it starts in and the map coming out of it are the motion's (port_overlay.cpp)
  if (g_nState == INTRO_ARMED)
    return true;
  MakeArt();
  const double t = Since();
  const float f = static_cast<float>((t - kCountdownAt) * kFps);
  if (f >= 0.0f && f < INTRO_FRAMES) {
    Stage st;
    st.k = H / INTRO_STAGE_H;
    st.ox = (W - INTRO_STAGE_W * st.k) / 2;
    Countdown(st, nWidth, nHeight, f);
  }
  return true;
}

// The countdown as a layer over the game (port_overlay.h), the top one, holding the player's
// input while it holds the game.
const PortOverlayLayer kLayer = {
    "intro", PORT_OVERLAY_INTRO, 0,
    PortIntroArmed, NULL, LayerDraw, LayerHold, NULL, NULL, NULL};
PORT_OVERLAY_LAYER(kLayer);
