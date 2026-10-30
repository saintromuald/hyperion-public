// port_front_colours.cpp -- the colours a set-up's row may take (port_front_colours.h; the
// design center's 11-front-end, lobby.js's colour model and teams.js's picker, the user,
// 2026-10-26: "full color wheel selection mode for custom color ... along with expanded color
// swatch option").
//
// The 65 are the game's own (PlayersColourRamp): their swatches, shades and dots are read from
// a tileset's palette, whose team colour entries are the same in every tileset, so nothing of
// the game's is kept here but the names. Presentation only.
#include <windows.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <algorithm>
#include <map>
#include <string>
#include <vector>
#include <SDL3/SDL.h>
#include "port_front_colours.h"
#include "port_frontkit.h"
#include "port_frontlobby.h"  // kSlotColours
#include "port_ui.h"
#include "../../h/Storm.h"           // SFileLoadFile
#include "../../h/playertables.h"    // PlayersColourRamp, PLAYER_COLOURS
#include "../data/port_stormapi.h"   // SFileUnloadFile
#include "../platform/port_winmsg.h"  // WinMsgKeyState
#include "../render/port_teamcolor.h"  // PortRgbToLab, PortLabToRgb

static_assert(FrontKit::COLOUR_GAME == PLAYER_COLOURS, "the set-up offers the game's colours");

namespace FrontKit {

namespace {

// tools/hxmap/names.py's names, as a player reads them: its _hx colours, Hyperion's own of a
// name 1.17 has, are numbered as the other repeats are, with no mark of whose they are (the
// user, 2026-10-26)
const char *const kNames[COLOUR_GAME] = {
    PT_("Red"), PT_("Blue"), PT_("Teal"), PT_("Purple"), PT_("Orange"), PT_("Brown"), PT_("White"), PT_("Yellow"),
    PT_("Green"), PT_("Pale Yellow"), PT_("Tan"), PT_("Azure"),
    PT_("Pale Green"), PT_("Blueish Grey"), PT_("Pale Yellow 2"), PT_("Cyan"), PT_("Pink"), PT_("Olive"), PT_("Lime"),
    PT_("Navy"), PT_("Magenta"), PT_("Grey"),
    PT_("Black"), PT_("Electric Blue"), PT_("Forest"), PT_("Cream"), PT_("Sky"), PT_("Yellow 7"), PT_("Teal 2"),
    PT_("Maroon"), PT_("Moss"),
    PT_("Cornflower"), PT_("Yellow 2"), PT_("Pale Yellow 3"), PT_("Green 3"), PT_("Orange 7"), PT_("Red 3"),
    PT_("Amber"), PT_("Green 2"), PT_("Red 2"), PT_("Dusty Mint"), PT_("Pale Dusty Aqua"), PT_("Yellow 3"),
    PT_("Blue 4"), PT_("Mauve"),
    PT_("Deep Green"), PT_("Indigo"), PT_("Orange 2"), PT_("Blue 2"), PT_("Orange 3"), PT_("Deep Red"),
    PT_("Orange 4"), PT_("Yellow 4"), PT_("Blue 3"),
    PT_("Orange 5"), PT_("Dusty Green"), PT_("Orange 6"), PT_("White 2"), PT_("Deep Amber"), PT_("Dusty Indigo"),
    PT_("Dusty Orange"),
    PT_("Dusty Orange 2"), PT_("Deep Red 2"), PT_("Yellow 5"), PT_("Yellow 6")};

const int kTunitRamps = 16;  // the ramps an RGB colour is fitted to: 1.17's sixteen

const int kFieldSlot = 25, kHueSlot = 43;  // the picture atlas's (port_ui.h)
const int kFieldW = 192, kFieldH = 120;

enum { FOCUS_NONE, FOCUS_HEX, FOCUS_R, FOCUS_G, FOCUS_B, FOCUS_FIELD, FOCUS_HUE };

// The 65 as the game draws them, read once.
struct Model {
  bool bRead = false;
  bool bPalette = false;
  unsigned char abPal[1024] = {};
  unsigned anSwatch[COLOUR_GAME] = {};
  ColourLook looks[COLOUR_GAME];
  std::vector<int> order;
};
Model g_model;
std::map<unsigned, ColourLook> g_anyLooks;  // any colour's, by rgb

// The Any colour field's state: the hue, saturation and value being picked (kept, so a grey
// keeps its hue), the colour they were made from, the focus and what is typed.
float g_hsv[3];
unsigned g_hsvOf = 0xffffffffu;
int g_nFocus, g_nDrag;
std::string g_entry;
float g_fFieldHue = -1.0f;
bool g_bHueMade, g_bDrawn;

unsigned Pack(const float *pRgb)
{
  auto c = [](float f) { return static_cast<unsigned>(SDL_clamp(f * 255.0f + 0.5f, 0.0f, 255.0f)); };
  return c(pRgb[0]) << 16 | c(pRgb[1]) << 8 | c(pRgb[2]);
}

void Lab(unsigned rgb, float *pLab)
{
  const float f[3] = {(rgb >> 16 & 0xff) / 255.0f, (rgb >> 8 & 0xff) / 255.0f, (rgb & 0xff) / 255.0f};
  PortRgbToLab(f, pLab);
}

float LabGap(const float *a, const float *b)
{
  return sqrtf((a[0] - b[0]) * (a[0] - b[0]) + (a[1] - b[1]) * (a[1] - b[1]) + (a[2] - b[2]) * (a[2] - b[2]));
}

unsigned Entry(int nEntry)
{
  const unsigned char *p = &g_model.abPal[4 * (nEntry & 0xff)];
  return static_cast<unsigned>(p[0]) << 16 | p[1] << 8 | p[2];
}

void Read(void)
{
  Model &m = g_model;
  if (m.bRead)
    return;
  m.bRead = true;
  void *p = NULL;
  DWORD cb = 0;
  if (SFileLoadFile("tileset\\badlands.wpe", &p, &cb, 0, NULL) && p != NULL) {
    if (cb >= sizeof m.abPal) {
      memcpy(m.abPal, p, sizeof m.abPal);
      m.bPalette = true;
    }
    SFileUnloadFile(p);
  }
  for (int c = 0; c < COLOUR_GAME; ++c) {
    ColourLook &l = m.looks[c];
    unsigned char abRamp[8], bMinimap;
    if (m.bPalette && PlayersColourRamp(c, abRamp, &bMinimap)) {
      for (int k = 0; k < 8; ++k)
        l.anShade[k] = Entry(abRamp[k]);
      l.nMinimap = Entry(bMinimap);
    } else {
      // no palette: the lobby's own swatches, flat
      const unsigned rgb = c < 11 ? kSlotColours[c] : 0x808080;
      for (int k = 0; k < 8; ++k)
        l.anShade[k] = rgb;
      l.nMinimap = rgb;
    }
    m.anSwatch[c] = l.anShade[0];
    for (int k = 0; k < 8; ++k)
      Lab(l.anShade[k], l.afShade[k]);
    Lab(l.nMinimap, l.afMinimap);
  }
  // the picker's order (lobby.js HUE_ORDER): by the dot's hue from red round to the pinks, the
  // greys last, lightest first
  m.order.clear();
  for (int c = 0; c < COLOUR_GAME; ++c)
    m.order.push_back(c);
  std::stable_sort(m.order.begin(), m.order.end(), [&](int a, int b) {
    const float *la = m.looks[a].afMinimap, *lb = m.looks[b].afMinimap;
    const bool ga = hypotf(la[1], la[2]) < 0.045f, gb = hypotf(lb[1], lb[2]) < 0.045f;
    if (ga != gb)
      return gb;
    if (ga)
      return la[0] > lb[0];
    auto Hue = [](const float *l) {
      const float h = atan2f(l[2], l[1]);
      return h < 0.35f ? h + 6.2831853f : h;  // OKLab's red is near 0.5
    };
    const float ha = Hue(la), hb = Hue(lb);
    return ha != hb ? ha < hb : la[0] > lb[0];
  });
}

// Any colour as the game draws it (lobby.js fitRgb): the shades follow the ramp of 1.17's
// sixteen whose first entry is nearest, each shade's lightness and chroma the same share of the
// colour's as the ramp's are of its first, its hue turned as the ramp's turns; the dot is the
// nearest entry of the sixteen ramps.
ColourLook Fit(unsigned rgb)
{
  Read();
  const Model &m = g_model;
  ColourLook l = {};
  float lab[3];
  Lab(rgb, lab);
  int nRef = 0;
  float fBest = 9.0f;
  for (int c = 0; c < kTunitRamps; ++c) {
    const float d = LabGap(m.looks[c].afShade[0], lab);
    if (d < fBest)
      fBest = d, nRef = c;
  }
  const ColourLook &r = m.looks[nRef];
  const float L0 = r.afShade[0][0], C0 = hypotf(r.afShade[0][1], r.afShade[0][2]),
              H0 = atan2f(r.afShade[0][2], r.afShade[0][1]);
  const float L = lab[0], C = hypotf(lab[1], lab[2]), H = atan2f(lab[2], lab[1]);
  const bool bGrey = C0 < 0.02f;
  for (int k = 0; k < 8; ++k) {
    const float Ck = hypotf(r.afShade[k][1], r.afShade[k][2]);
    const float lk = L0 > 0.0f ? L * r.afShade[k][0] / L0 : L;
    const float ck = bGrey ? C : C * Ck / C0, hk = bGrey ? H : H + atan2f(r.afShade[k][2], r.afShade[k][1]) - H0;
    const float o[3] = {lk, ck * cosf(hk), ck * sinf(hk)};
    float f[3];
    PortLabToRgb(o, f);
    l.anShade[k] = Pack(f);
    Lab(l.anShade[k], l.afShade[k]);
  }
  l.nMinimap = rgb;
  float fNear = 9.0f;
  for (int c = 0; c < kTunitRamps; ++c)
    for (int k = 0; k < 8; ++k) {
      const float d = LabGap(m.looks[c].afShade[k], lab);
      if (d < fNear)
        fNear = d, l.nMinimap = m.looks[c].anShade[k];
    }
  Lab(l.nMinimap, l.afMinimap);
  return l;
}

float RampGap(const ColourLook &a, const ColourLook &b)
{
  float t = 0.0f;
  for (int k = 0; k < 8; ++k)
    t += LabGap(a.afShade[k], b.afShade[k]);
  return t / 8.0f;
}

// ---------------------------------------------------------------------------
// The Any colour field (port_front_theme.cpp's picker, for one colour).

std::string Hex(unsigned n)
{
  char sz[8];
  SDL_snprintf(sz, sizeof sz, "#%06X", n & 0xffffff);
  return sz;
}

void SyncHsv(unsigned rgb)
{
  if (g_hsvOf != rgb) {
    ThemeHsv(rgb, &g_hsv[0], &g_hsv[1], &g_hsv[2]);
    g_hsvOf = rgb;
  }
}

void Commit(unsigned *pRgb)
{
  *pRgb = g_hsvOf = ThemeFromHsv(g_hsv[0], g_hsv[1], g_hsv[2]);
}

void MakeField(void)
{
  if (g_hsv[0] == g_fFieldHue)
    return;
  g_fFieldHue = g_hsv[0];
  std::vector<unsigned char> px(static_cast<size_t>(kFieldW) * kFieldH * 4);
  for (int y = 0; y < kFieldH; ++y)
    for (int x = 0; x < kFieldW; ++x) {
      const unsigned n = ThemeFromHsv(g_fFieldHue, x / static_cast<float>(kFieldW - 1), 1.0f - y / static_cast<float>(kFieldH - 1));
      unsigned char *q = &px[(static_cast<size_t>(y) * kFieldW + x) * 4];
      q[0] = (n >> 16) & 0xff, q[1] = (n >> 8) & 0xff, q[2] = n & 0xff, q[3] = 255;
    }
  UiPictureSet(kFieldSlot, px.data(), kFieldW, kFieldH);
}

void MakeHue(void)
{
  if (g_bHueMade)
    return;
  g_bHueMade = true;
  const int w = 4, h = 256;
  std::vector<unsigned char> px(static_cast<size_t>(w) * h * 4);
  for (int y = 0; y < h; ++y) {
    const unsigned n = ThemeFromHsv(y * 360.0f / h, 1.0f, 1.0f);
    for (int x = 0; x < w; ++x) {
      unsigned char *q = &px[(static_cast<size_t>(y) * w + x) * 4];
      q[0] = (n >> 16) & 0xff, q[1] = (n >> 8) & 0xff, q[2] = n & 0xff, q[3] = 255;
    }
  }
  UiPictureSet(kHueSlot, px.data(), w, h);
}

// The field, its picture and the dot at the picked saturation and value (n: 1 focused).
void DrawField(float x, float y, float w, float h, void *pCtx)
{
  const bool bFocus = static_cast<const ScDraw *>(pCtx)->n != 0;
  UiPicture(x, y, w, h, kFieldSlot, KitRgb(0xffffff), Sc(3));
  UiRing(x, y, w, h, bFocus ? kScTabOn.rim : kScDdRim, Sc(3), fmaxf(1.0f, Sc(bFocus ? 2 : 1)));
  const float cx = x + g_hsv[1] * w, cy = y + (1.0f - g_hsv[2]) * h, r = Sc(8);
  UiFill(cx - r, cy - r, 2 * r, 2 * r, KitRgb(ThemeFromHsv(g_hsv[0], g_hsv[1], g_hsv[2])), r);
  UiRing(cx - r - 1.0f, cy - r - 1.0f, 2 * r + 2.0f, 2 * r + 2.0f, KitRgb(0x000000), r + 1.0f, 1.0f);
  UiRing(cx - r, cy - r, 2 * r, 2 * r, KitRgb(0xffffff), r, fmaxf(1.5f, Sc(2)));
}

// The hue strip and the bar at the picked hue.
void DrawHue(float x, float y, float w, float h, void *pCtx)
{
  const bool bFocus = static_cast<const ScDraw *>(pCtx)->n != 0;
  UiPicture(x, y, w, h, kHueSlot, KitRgb(0xffffff), Sc(3));
  UiRing(x, y, w, h, bFocus ? kScTabOn.rim : kScDdRim, Sc(3), fmaxf(1.0f, Sc(bFocus ? 2 : 1)));
  const float by = y + g_hsv[0] / 360.0f * h, bh = Sc(6);
  UiRing(x - Sc(4) - 1.0f, by - bh / 2 - 1.0f, w + Sc(8) + 2.0f, bh + 2.0f, KitRgb(0x000000), Sc(2) + 1.0f, 1.0f);
  UiRing(x - Sc(4), by - bh / 2, w + Sc(8), bh, KitRgb(0xffffff), Sc(2), fmaxf(1.5f, Sc(2)));
}

// An entry's face: a dark field, its rim lit while it has the keyboard.
void DrawEntry(float x, float y, float w, float h, void *pCtx)
{
  const bool bFocus = static_cast<const ScDraw *>(pCtx)->n != 0;
  UiFill(x, y, w, h, kScFill, Sc(3));
  UiRing(x, y, w, h, bFocus ? kScTabOn.rim : kScDdRim, Sc(3), fmaxf(1.0f, Sc(1)));
}

std::string EntryText(unsigned rgb, int nFocus)
{
  if (nFocus == FOCUS_HEX)
    return Hex(rgb);
  const int nShift = nFocus == FOCUS_R ? 16 : nFocus == FOCUS_G ? 8 : 0;
  return std::to_string((rgb >> nShift) & 0xff);
}

void SetColour(unsigned *pRgb, unsigned n)
{
  *pRgb = n;
  g_hsvOf = 0xffffffffu;
  SyncHsv(n);
}

// What was typed into the focused entry, applied as soon as it makes a colour.
bool Typed(unsigned *pRgb)
{
  if (g_nFocus < FOCUS_HEX || g_nFocus > FOCUS_B)
    return false;
  bool bChanged = false;
  for (char c : g_typed) {
    const bool bHexDigit = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
    if (g_nFocus == FOCUS_HEX ? (bHexDigit || (c == '#' && g_entry.empty())) && g_entry.size() < 7
                              : c >= '0' && c <= '9' && g_entry.size() < 3) {
      g_entry.push_back(c);
      bChanged = true;
    }
  }
  if (Key(VK_BACK) && !g_entry.empty()) {
    g_entry.pop_back();
    bChanged = true;
  }
  if (Key(VK_RETURN) || Key(VK_TAB) || Key(VK_ESCAPE))
    g_nFocus = FOCUS_NONE;
  if (!bChanged)
    return false;
  const unsigned was = *pRgb;
  if (g_nFocus == FOCUS_HEX) {
    const std::string s = !g_entry.empty() && g_entry[0] == '#' ? g_entry.substr(1) : g_entry;
    if (s.size() == 6)
      SetColour(pRgb, static_cast<unsigned>(strtoul(s.c_str(), NULL, 16)));
  } else if (!g_entry.empty()) {
    const unsigned v = static_cast<unsigned>(SDL_min(255, atoi(g_entry.c_str())));
    const int nShift = g_nFocus == FOCUS_R ? 16 : g_nFocus == FOCUS_G ? 8 : 0;
    SetColour(pRgb, (*pRgb & ~(0xffu << nShift)) | (v << nShift));
  }
  return *pRgb != was;
}

void ValueEntry(unsigned *pRgb, bool bLive, int nFocus, const char *pszLabel, bool bWide)
{
  Decl lab;
  lab.d.layout.layoutDirection = CLAY_TOP_TO_BOTTOM;
  lab.d.layout.sizing.width = bWide ? Grow() : Fixed(Sc(46));
  lab.d.layout.childGap = Gap(Fs(3));
  Open(lab);
  Text(pszLabel, UI_FACE_TEXT, Fs(12), 0.0f, kScInkMsg);
  Decl b;
  b.d.id = Id("colentry", nFocus);
  const bool bFocus = g_nFocus == nFocus;
  if (bLive && Clicked(b.d.id) && !bFocus) {
    g_nFocus = nFocus;
    g_entry = EntryText(*pRgb, nFocus);
  }
  b.d.layout.sizing.width = Grow();
  b.d.layout.sizing.height = Fixed(Sc(28));
  b.d.layout.padding = Pad(Fs(6), Fs(6), 0, 0);
  b.d.layout.childAlignment.y = CLAY_ALIGN_Y_CENTER;
  b.d.custom.customData = Custom(DrawEntry, kScTab, 0.0f, bFocus ? 1 : 0);
  Open(b);
  Text(Keep(bFocus ? g_entry : EntryText(*pRgb, nFocus)), UI_FACE_TEXT, Fs(14), 0.0f, kScInkHi);
  if (bFocus && (SDL_GetTicks() / 530) % 2 == 0) {
    Decl caret;
    caret.d.layout.sizing.width = Fixed(fmaxf(1.0f, Sc(2)));
    caret.d.layout.sizing.height = Fixed(Sc(17));
    caret.d.backgroundColor = Col(kScInkHi);
    Open(caret);
    Close();
  }
  Close();
  Close();
}

}  // namespace

const char *ColourName(int nColour)
{
  return nColour >= 0 && nColour < COLOUR_GAME ? PortText(kNames[nColour]) : PortText("Own colour");
}

unsigned ColourRgb(int nColour)
{
  Read();
  return nColour >= 0 && nColour < COLOUR_GAME ? g_model.anSwatch[nColour] : 0xb4b4b4;
}

const std::vector<int> &ColourOrder(void)
{
  Read();
  return g_model.order;
}

const ColourLook &ColourLookOf(int nColour, unsigned rgb)
{
  Read();
  if (nColour >= 0 && nColour < COLOUR_GAME)
    return g_model.looks[nColour];
  auto it = g_anyLooks.find(rgb & 0xffffff);
  if (it == g_anyLooks.end()) {
    if (g_anyLooks.size() > 256)
      g_anyLooks.clear();
    it = g_anyLooks.emplace(rgb & 0xffffff, Fit(rgb & 0xffffff)).first;
  }
  return it->second;
}

float ColourGap(int nA, unsigned rgbA, int nB, unsigned rgbB)
{
  const ColourLook a = ColourLookOf(nA, rgbA), &b = ColourLookOf(nB, rgbB);
  return fminf(LabGap(a.afMinimap, b.afMinimap), RampGap(a, b));
}

const char *ColourCloseOn(int nA, unsigned rgbA, int nB, unsigned rgbB)
{
  const ColourLook a = ColourLookOf(nA, rgbA), &b = ColourLookOf(nB, rgbB);
  return LabGap(a.afMinimap, b.afMinimap) <= RampGap(a, b) ? PortText("on the minimap") : PortText("in the units");
}

bool ColourAnyField(unsigned *pRgb, bool bLive)
{
  g_bDrawn = true;
  SyncHsv(*pRgb);
  const unsigned was = *pRgb;
  const Clay_ElementId field = Id("colfield"), hue = Id("colhue");
  // a press anywhere but the entry being typed in leaves it
  if (g_bPressed && g_nFocus >= FOCUS_HEX && g_nFocus <= FOCUS_B && !Over(Id("colentry", g_nFocus)))
    g_nFocus = FOCUS_NONE;
  if (g_bPressed && (g_nFocus == FOCUS_FIELD || g_nFocus == FOCUS_HUE) && !Over(field) && !Over(hue))
    g_nFocus = FOCUS_NONE;
  // pressed on, so the press is the picker's and does not close it (PulldownCloseOutside)
  if (bLive) {
    Clicked(field);
    Clicked(hue);
  }
  if (bLive && g_bPressed && Over(field))
    g_nDrag = g_nFocus = FOCUS_FIELD;
  if (bLive && g_bPressed && Over(hue))
    g_nDrag = g_nFocus = FOCUS_HUE;
  if (!g_bDown)
    g_nDrag = FOCUS_NONE;
  if (g_nDrag == FOCUS_FIELD) {
    const Clay_ElementData e = Clay_GetElementData(field);
    if (e.found && e.boundingBox.width > 0.0f) {
      g_hsv[1] = SDL_clamp((g_mouseX - e.boundingBox.x) / e.boundingBox.width, 0.0f, 1.0f);
      g_hsv[2] = 1.0f - SDL_clamp((g_mouseY - e.boundingBox.y) / e.boundingBox.height, 0.0f, 1.0f);
      Commit(pRgb);
    }
  } else if (g_nDrag == FOCUS_HUE) {
    const Clay_ElementData e = Clay_GetElementData(hue);
    if (e.found && e.boundingBox.height > 0.0f) {
      g_hsv[0] = fminf(359.9f, SDL_clamp((g_mouseY - e.boundingBox.y) / e.boundingBox.height, 0.0f, 1.0f) * 360.0f);
      Commit(pRgb);
    }
  }
  if (bLive && (g_nFocus == FOCUS_FIELD || g_nFocus == FOCUS_HUE)) {
    const bool bShift = WinMsgKeyState(VK_SHIFT) < 0;
    const float d = g_nFocus == FOCUS_FIELD ? (bShift ? 0.1f : 0.01f) : (bShift ? 10.0f : 1.0f);
    bool bMoved = false;
    if (g_nFocus == FOCUS_FIELD) {
      if (Key(VK_LEFT))
        g_hsv[1] = SDL_clamp(g_hsv[1] - d, 0.0f, 1.0f), bMoved = true;
      if (Key(VK_RIGHT))
        g_hsv[1] = SDL_clamp(g_hsv[1] + d, 0.0f, 1.0f), bMoved = true;
      if (Key(VK_UP))
        g_hsv[2] = SDL_clamp(g_hsv[2] + d, 0.0f, 1.0f), bMoved = true;
      if (Key(VK_DOWN))
        g_hsv[2] = SDL_clamp(g_hsv[2] - d, 0.0f, 1.0f), bMoved = true;
    } else {
      if (Key(VK_UP) || Key(VK_LEFT))
        g_hsv[0] = fmodf(g_hsv[0] - d + 360.0f, 360.0f), bMoved = true;
      if (Key(VK_DOWN) || Key(VK_RIGHT))
        g_hsv[0] = fmodf(g_hsv[0] + d, 360.0f), bMoved = true;
    }
    if (bMoved)
      Commit(pRgb);
  }
  if (bLive)
    Typed(pRgb);
  MakeField();
  MakeHue();
  Decl row;
  row.d.layout.sizing.width = Grow();
  row.d.layout.sizing.height = Fixed(Sc(170));
  row.d.layout.childGap = Gap(Fs(10));
  Open(row);
  Decl f;
  f.d.id = field;
  f.d.layout.sizing.width = Grow();
  f.d.layout.sizing.height = Grow();
  f.d.custom.customData = Custom(DrawField, kScTab, 0.0f, g_nFocus == FOCUS_FIELD ? 1 : 0);
  Open(f);
  Close();
  Decl h;
  h.d.id = hue;
  h.d.layout.sizing.width = Fixed(Sc(22));
  h.d.layout.sizing.height = Grow();
  h.d.custom.customData = Custom(DrawHue, kScTab, 0.0f, g_nFocus == FOCUS_HUE ? 1 : 0);
  Open(h);
  Close();
  Close();
  return *pRgb != was;
}

void ColourAnyValues(unsigned *pRgb, bool bLive)
{
  Decl row;
  row.d.layout.sizing.width = Grow();
  row.d.layout.childGap = Gap(Fs(6));
  Open(row);
  ValueEntry(pRgb, bLive, FOCUS_HEX, PortText("Hex"), true);
  ValueEntry(pRgb, bLive, FOCUS_R, PortTextC("red", "R"), false);
  ValueEntry(pRgb, bLive, FOCUS_G, PortTextC("green", "G"), false);
  ValueEntry(pRgb, bLive, FOCUS_B, PortTextC("blue", "B"), false);
  Close();
}

bool ColourAnyTyping(void)
{
  // a field no longer shown (the pop closed, the other tab) gives the keys back
  if (!g_bDrawn)
    g_nFocus = FOCUS_NONE;
  g_bDrawn = false;
  return g_nFocus >= FOCUS_HEX && g_nFocus <= FOCUS_B;
}

bool ColourAnyEntryFocused(void)
{
  return g_nFocus >= FOCUS_HEX && g_nFocus <= FOCUS_B;
}

void ColourAnyReset(void)
{
  g_nFocus = g_nDrag = FOCUS_NONE;
  g_entry.clear();
  g_hsvOf = 0xffffffffu;
}

}  // namespace FrontKit
