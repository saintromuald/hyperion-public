// port_front_theme.cpp -- Settings' colour theme (Interface;
// docs/modules/front-end.md).
// The presets and Custom as buttons, each with its panels, accent and second accent;
// under Custom, one picker for the three colours (the user, 2026-10-14: "A color picker
// gradiant selection is better, like a full color system selection"): a chip for which
// colour, a saturation and brightness field with a hue strip beside it, hex and R G B, whether
// lightness is the player's or the theme's, what the screen draws, and a preset to start
// from. The whole screen follows each change (port_frontkit.cpp, ThemeShow), and nothing is
// kept until Settings' Accept.
#include <windows.h>
#include <math.h>
#include <stdlib.h>
#include <string>
#include <vector>
#include <SDL3/SDL.h>
#include "port_frontkit.h"
#include "../platform/port_winmsg.h"
#include "port_frontscreens.h"
#include "port_ui.h"

namespace FrontKit {

namespace {

const int kFieldSlot = 22;  // the picture atlas's slots for the field and the hue strip
const int kHueSlot = 23;
const int kFieldW = 192, kFieldH = 120;
const int kCustomTheme = 5;

// The three colours, in the chips' order.
const char *const kSlotNames[] = {PT_("Panels"), PT_("Accent"), PT_("Second accent")};
const char *const kSlotWhat[] = {PT_("Under every panel"), PT_("Rims, text, glows and controls"),
                                 PT_("Start Game, Accept and Activate")};

enum { FOCUS_NONE, FOCUS_HEX, FOCUS_R, FOCUS_G, FOCUS_B, FOCUS_FIELD, FOCUS_HUE };

int g_nSlot = 1;               // the colour being picked: the accent first, as the design opens
float g_hsv[3][3];             // each colour's hue, saturation and value, kept apart so a grey keeps its hue
unsigned int g_hsvOf[3] = {0xffffffffu, 0xffffffffu, 0xffffffffu};  // the colour each was made from
int g_nFocus;                  // FOCUS_*
std::string g_entry;           // the entry being typed in
int g_nDrag;                   // FOCUS_FIELD or FOCUS_HUE while dragged
float g_fFieldHue = -1.0f;     // the field's picture: its hue,
bool g_bFieldCap;              // and whether it shades what the panels may not be
bool g_bHueMade;
ThemeCustom g_reported;        // the readout's report, for the custom it was made for
ThemeReport g_report;
bool g_bReported;
bool g_bDrawn;                 // the picker was laid out since the dialog last read its keys

unsigned int &Colour(ThemeCustom *p, int k)
{
  return k == 0 ? p->panel : k == 1 ? p->accent : p->accent2;
}

std::string Hex(unsigned int n)
{
  char sz[8];
  SDL_snprintf(sz, sizeof sz, "#%06x", n & 0xffffff);
  return sz;
}

// The hsv kept for each colour, made again where the colour changed from outside (a preset,
// Restore Defaults, Settings opened again).
void SyncHsv(ThemeCustom *p)
{
  for (int k = 0; k < 3; ++k)
    if (g_hsvOf[k] != Colour(p, k)) {
      ThemeHsv(Colour(p, k), &g_hsv[k][0], &g_hsv[k][1], &g_hsv[k][2]);
      g_hsvOf[k] = Colour(p, k);
    }
}

// The picked colour from its hsv.
void Commit(ThemeCustom *p)
{
  const float *o = g_hsv[g_nSlot];
  Colour(p, g_nSlot) = g_hsvOf[g_nSlot] = ThemeFromHsv(o[0], o[1], o[2]);
}

void SetColour(ThemeCustom *p, unsigned int n)
{
  Colour(p, g_nSlot) = n;
  g_hsvOf[g_nSlot] = 0xffffffffu;
  SyncHsv(p);
}

// The field for the slot's hue; with the player's lightness, the panels' field shades what
// they may not reach (they stay dark).
void MakeField(bool bCap)
{
  const float h = g_hsv[g_nSlot][0];
  if (h == g_fFieldHue && bCap == g_bFieldCap)
    return;
  g_fFieldHue = h;
  g_bFieldCap = bCap;
  std::vector<unsigned char> px(static_cast<size_t>(kFieldW) * kFieldH * 4);
  for (int y = 0; y < kFieldH; ++y)
    for (int x = 0; x < kFieldW; ++x) {
      unsigned int n = ThemeFromHsv(h, x / static_cast<float>(kFieldW - 1), 1.0f - y / static_cast<float>(kFieldH - 1));
      float r = ((n >> 16) & 0xff), g = ((n >> 8) & 0xff), b = (n & 0xff);
      if (bCap && ThemeLightOf(n) > kThemePanelMax) {
        const float st = (x + y) % 10 < 2 ? 0.5f : 0.3f;
        r *= st, g *= st, b *= st;
      }
      unsigned char *q = &px[(static_cast<size_t>(y) * kFieldW + x) * 4];
      q[0] = static_cast<unsigned char>(r), q[1] = static_cast<unsigned char>(g), q[2] = static_cast<unsigned char>(b);
      q[3] = 255;
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
    const unsigned int n = ThemeFromHsv(y * 360.0f / h, 1.0f, 1.0f);
    for (int x = 0; x < w; ++x) {
      unsigned char *q = &px[(static_cast<size_t>(y) * w + x) * 4];
      q[0] = (n >> 16) & 0xff, q[1] = (n >> 8) & 0xff, q[2] = n & 0xff, q[3] = 255;
    }
  }
  UiPictureSet(kHueSlot, px.data(), w, h);
}

// ---------------------------------------------------------------------------
// Drawing.

// A theme's swatch: its panels, accent and second accent side by side (look.top, bot and
// rim), each bar f units wide, in a faint white rim.
void DrawSwatch(float x, float y, float w, float h, void *pCtx)
{
  const ScLook &l = static_cast<const ScDraw *>(pCtx)->look;
  const float bw = w / 3.0f;
  UiFill(x, y, bw + 0.5f, h, l.top);
  UiFill(x + bw, y, bw + 0.5f, h, l.bot);
  UiFill(x + 2 * bw, y, w - 2 * bw, h, l.rim);
  UiRing(x, y, w, h, KitRgb(0xffffff, 0.25f), Sc(3), fmaxf(1.0f, Sc(1)));
}

void Swatch(const ThemeCustom &t, float fBar, float fTall)
{
  Decl sw;
  sw.d.layout.sizing.width = Fixed(Sc(fBar * 3));
  sw.d.layout.sizing.height = Fixed(Sc(fTall));
  ScLook look = kScTab;
  look.top = KitRgb(t.panel);
  look.bot = KitRgb(t.accent);
  look.rim = KitRgb(t.accent2);
  sw.d.custom.customData = Custom(DrawSwatch, look);
  Open(sw);
  Close();
}

// The field, its picture and the dot at the picked saturation and value (n: 1 focused).
void DrawField(float x, float y, float w, float h, void *pCtx)
{
  const bool bFocus = static_cast<const ScDraw *>(pCtx)->n != 0;
  UiPicture(x, y, w, h, kFieldSlot, KitRgb(0xffffff), Sc(3));
  UiRing(x, y, w, h, bFocus ? kScTabOn.rim : kScDdRim, Sc(3), fmaxf(1.0f, Sc(bFocus ? 2 : 1)));
  const float *o = g_hsv[g_nSlot];
  const float cx = x + o[1] * w, cy = y + (1.0f - o[2]) * h, r = Sc(8);
  UiFill(cx - r, cy - r, 2 * r, 2 * r, KitRgb(ThemeFromHsv(o[0], o[1], o[2])), r);
  UiRing(cx - r - 1.0f, cy - r - 1.0f, 2 * r + 2.0f, 2 * r + 2.0f, KitRgb(0x000000), r + 1.0f, 1.0f);
  UiRing(cx - r, cy - r, 2 * r, 2 * r, KitRgb(0xffffff), r, fmaxf(1.5f, Sc(2)));
}

// The hue strip and the bar at the picked hue.
void DrawHue(float x, float y, float w, float h, void *pCtx)
{
  const bool bFocus = static_cast<const ScDraw *>(pCtx)->n != 0;
  UiPicture(x, y, w, h, kHueSlot, KitRgb(0xffffff), Sc(3));
  UiRing(x, y, w, h, bFocus ? kScTabOn.rim : kScDdRim, Sc(3), fmaxf(1.0f, Sc(bFocus ? 2 : 1)));
  const float by = y + g_hsv[g_nSlot][0] / 360.0f * h, bh = Sc(6);
  UiRing(x - Sc(4) - 1.0f, by - bh / 2 - 1.0f, w + Sc(8) + 2.0f, bh + 2.0f, KitRgb(0x000000), Sc(2) + 1.0f, 1.0f);
  UiRing(x - Sc(4), by - bh / 2, w + Sc(8), bh, KitRgb(0xffffff), Sc(2), fmaxf(1.5f, Sc(2)));
}

// An entry's face (front.css .d-cp-vals input): a dark field, its rim lit while it has the
// keyboard.
void DrawEntry(float x, float y, float w, float h, void *pCtx)
{
  const bool bFocus = static_cast<const ScDraw *>(pCtx)->n != 0;
  UiFill(x, y, w, h, kScFill, Sc(3));
  UiRing(x, y, w, h, bFocus ? kScTabOn.rim : kScDdRim, Sc(3), fmaxf(1.0f, Sc(1)));
}

// A flat button of the picker (front.css .d-cp-seg, .d-cp-slot, .d-theme-opt), on when bOn.
ScLook TabLook(bool bOn, bool bHot)
{
  return bOn ? kScTabOn : bHot ? kScTabHi : kScTab;
}

// ---------------------------------------------------------------------------
// The picker's parts.

void Chips(ThemeCustom *p, bool bLive)
{
  Decl row;
  row.d.layout.sizing.width = Grow();
  row.d.layout.childGap = Gap(Fs(6));
  Open(row);
  for (int k = 0; k < 3; ++k) {
    Decl b;
    b.d.id = Id("cpchip", k);
    const bool bOn = k == g_nSlot;
    const bool bHot = bLive && Over(b.d.id);
    if (bLive && Clicked(b.d.id) && !bOn) {
      g_nSlot = k;
      g_nFocus = FOCUS_NONE;
    }
    b.d.layout.layoutDirection = CLAY_TOP_TO_BOTTOM;
    b.d.layout.sizing.width = Grow();
    b.d.layout.padding = Pad(Fs(7), Fs(7), Fs(6), Fs(6));
    b.d.layout.childGap = Gap(Fs(5));
    b.d.custom.customData = Custom(DrawScRaised, TabLook(bOn, bHot));
    Open(b);
    Text(PortText(kSlotNames[k]), UI_FACE_BOLD, Fs(13), 0.0f, bOn || bHot ? kScInkHi : kScInkButton);
    Decl sw;
    sw.d.layout.sizing.width = Grow();
    sw.d.layout.sizing.height = Fixed(Sc(20));
    sw.d.layout.childAlignment.x = CLAY_ALIGN_X_CENTER;
    sw.d.layout.childAlignment.y = CLAY_ALIGN_Y_CENTER;
    sw.d.backgroundColor = Col(KitRgb(Colour(p, k)));
    sw.d.cornerRadius = {Sc(3), Sc(3), Sc(3), Sc(3)};
    Open(sw);
    Decl pill;
    pill.d.layout.padding = Pad(Fs(4), Fs(4), 0, 0);
    pill.d.backgroundColor = Col(KitRgb(0x000000, 0.55f));
    pill.d.cornerRadius = {Sc(2), Sc(2), Sc(2), Sc(2)};
    Open(pill);
    Text(Keep(Hex(Colour(p, k))), UI_FACE_TEXT, Fs(11), 0.0f, KitRgb(0xffffff));
    Close();
    Close();
    Close();
  }
  Close();
}

// The field and the hue strip, dragged or moved with the arrow keys (Shift for steps of 10).
void Pick(ThemeCustom *p, bool bLive)
{
  const Clay_ElementId field = Id("cpfield"), hue = Id("cphue");
  if (bLive && g_bPressed && Over(field))
    g_nDrag = g_nFocus = FOCUS_FIELD;
  if (bLive && g_bPressed && Over(hue))
    g_nDrag = g_nFocus = FOCUS_HUE;
  if (!g_bDown)
    g_nDrag = FOCUS_NONE;
  float *o = g_hsv[g_nSlot];
  if (g_nDrag == FOCUS_FIELD) {
    const Clay_ElementData e = Clay_GetElementData(field);
    if (e.found && e.boundingBox.width > 0.0f) {
      o[1] = SDL_clamp((g_mouseX - e.boundingBox.x) / e.boundingBox.width, 0.0f, 1.0f);
      o[2] = 1.0f - SDL_clamp((g_mouseY - e.boundingBox.y) / e.boundingBox.height, 0.0f, 1.0f);
      Commit(p);
    }
  } else if (g_nDrag == FOCUS_HUE) {
    const Clay_ElementData e = Clay_GetElementData(hue);
    if (e.found && e.boundingBox.height > 0.0f) {
      o[0] = fminf(359.9f, SDL_clamp((g_mouseY - e.boundingBox.y) / e.boundingBox.height, 0.0f, 1.0f) * 360.0f);
      Commit(p);
    }
  }
  if (bLive && (g_nFocus == FOCUS_FIELD || g_nFocus == FOCUS_HUE)) {
    const bool bShift = WinMsgKeyState(VK_SHIFT) < 0;
    const float d = g_nFocus == FOCUS_FIELD ? (bShift ? 0.1f : 0.01f) : (bShift ? 10.0f : 1.0f);
    bool bMoved = false;
    if (g_nFocus == FOCUS_FIELD) {
      if (Key(VK_LEFT))
        o[1] = SDL_clamp(o[1] - d, 0.0f, 1.0f), bMoved = true;
      if (Key(VK_RIGHT))
        o[1] = SDL_clamp(o[1] + d, 0.0f, 1.0f), bMoved = true;
      if (Key(VK_UP))
        o[2] = SDL_clamp(o[2] + d, 0.0f, 1.0f), bMoved = true;
      if (Key(VK_DOWN))
        o[2] = SDL_clamp(o[2] - d, 0.0f, 1.0f), bMoved = true;
    } else {
      if (Key(VK_UP) || Key(VK_LEFT))
        o[0] = fmodf(o[0] - d + 360.0f, 360.0f), bMoved = true;
      if (Key(VK_DOWN) || Key(VK_RIGHT))
        o[0] = fmodf(o[0] + d, 360.0f), bMoved = true;
    }
    if (bMoved)
      Commit(p);
  }
  MakeField(g_nSlot == 0 && p->bLight);
  MakeHue();

  Decl row;
  row.d.layout.sizing.width = Grow();
  row.d.layout.sizing.height = Fixed(Sc(200));
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
  h.d.layout.sizing.width = Fixed(Sc(24));
  h.d.layout.sizing.height = Grow();
  h.d.custom.customData = Custom(DrawHue, kScTab, 0.0f, g_nFocus == FOCUS_HUE ? 1 : 0);
  Open(h);
  Close();
  Close();
}

// The text an entry shows while it does not have the keyboard.
std::string EntryText(ThemeCustom *p, int nFocus)
{
  const unsigned int n = Colour(p, g_nSlot);
  if (nFocus == FOCUS_HEX)
    return Hex(n);
  const int nShift = nFocus == FOCUS_R ? 16 : nFocus == FOCUS_G ? 8 : 0;
  return std::to_string((n >> nShift) & 0xff);
}

// What was typed into the focused entry, applied as soon as it makes a colour.
void Typed(ThemeCustom *p)
{
  if (g_nFocus < FOCUS_HEX || g_nFocus > FOCUS_B)
    return;
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
    return;
  if (g_nFocus == FOCUS_HEX) {
    const std::string s = !g_entry.empty() && g_entry[0] == '#' ? g_entry.substr(1) : g_entry;
    if (s.size() == 6)
      SetColour(p, static_cast<unsigned int>(strtoul(s.c_str(), NULL, 16)));
  } else if (!g_entry.empty()) {
    const unsigned int v = static_cast<unsigned int>(SDL_min(255, atoi(g_entry.c_str())));
    const int nShift = g_nFocus == FOCUS_R ? 16 : g_nFocus == FOCUS_G ? 8 : 0;
    SetColour(p, (Colour(p, g_nSlot) & ~(0xffu << nShift)) | (v << nShift));
  }
}

void Entry(ThemeCustom *p, bool bLive, int nFocus, const char *pszLabel, bool bWide)
{
  Decl lab;
  lab.d.layout.layoutDirection = CLAY_TOP_TO_BOTTOM;
  lab.d.layout.sizing.width = bWide ? Grow() : Fixed(Sc(64));
  lab.d.layout.childGap = Gap(Fs(3));
  Open(lab);
  Text(pszLabel, UI_FACE_TEXT, Fs(12), 0.0f, kScInkMsg);
  Decl b;
  b.d.id = Id("cpentry", nFocus);
  const bool bFocus = g_nFocus == nFocus;
  if (bLive && Clicked(b.d.id) && !bFocus) {
    g_nFocus = nFocus;
    g_entry = EntryText(p, nFocus);
  }
  b.d.layout.sizing.width = Grow();
  b.d.layout.sizing.height = Fixed(Sc(30));
  b.d.layout.padding = Pad(Fs(6), Fs(6), 0, 0);
  b.d.layout.childAlignment.y = CLAY_ALIGN_Y_CENTER;
  b.d.custom.customData = Custom(DrawEntry, kScTab, 0.0f, bFocus ? 1 : 0);
  Open(b);
  Text(Keep(bFocus ? g_entry : EntryText(p, nFocus)), UI_FACE_TEXT, Fs(14), 0.0f, kScInkHi);
  if (bFocus && (SDL_GetTicks() / 530) % 2 == 0) {
    Decl caret;
    caret.d.layout.sizing.width = Fixed(fmaxf(1.0f, Sc(2)));
    caret.d.layout.sizing.height = Fixed(Sc(18));
    caret.d.backgroundColor = Col(kScInkHi);
    Open(caret);
    Close();
  }
  Close();
  Close();
}

void Values(ThemeCustom *p, bool bLive)
{
  Decl row;
  row.d.layout.sizing.width = Grow();
  row.d.layout.childGap = Gap(Fs(6));
  Open(row);
  Entry(p, bLive, FOCUS_HEX, PortText("Hex"), true);
  Entry(p, bLive, FOCUS_R, PortTextC("red", "R"), false);
  Entry(p, bLive, FOCUS_G, PortTextC("green", "G"), false);
  Entry(p, bLive, FOCUS_B, PortTextC("blue", "B"), false);
  Close();
}

void RowLabel(const char *psz)
{
  Decl k;
  k.d.layout.sizing.width = Fixed(Sc(86));
  Open(k);
  Text(psz, UI_FACE_BOLD, Fs(14), 0.0f, kScInkLabel);
  Close();
}

// Lightness: the player's (the default) or the theme's, as the presets keep it.
void Lightness(ThemeCustom *p, bool bLive)
{
  Decl row;
  row.d.layout.childGap = Gap(Fs(6));
  row.d.layout.childAlignment.y = CLAY_ALIGN_Y_CENTER;
  Open(row);
  RowLabel(PortText("Lightness"));
  const char *const kWords[] = {PortText("Mine"), PortText("The theme\xE2\x80\x99s")};
  for (int k = 0; k < 2; ++k) {
    Decl b;
    b.d.id = Id("cpseg", k);
    const bool bOn = p->bLight == (k == 0);
    const bool bHot = bLive && Over(b.d.id);
    if (bLive && Clicked(b.d.id))
      p->bLight = k == 0;
    b.d.layout.padding = Pad(Fs(10), Fs(10), Fs(4), Fs(4));
    b.d.custom.customData = Custom(DrawScRaised, TabLook(bOn, bHot));
    Open(b);
    Text(kWords[k], UI_FACE_TEXT, Fs(14), 0.0f, bOn || bHot ? kScInkHi : kScInkButton);
    Close();
  }
  Close();
}

void OutSwatch(unsigned int n, const char *pszLabel)
{
  Decl c;
  c.d.layout.layoutDirection = CLAY_TOP_TO_BOTTOM;
  c.d.layout.childGap = Gap(Fs(3));
  c.d.layout.childAlignment.x = CLAY_ALIGN_X_CENTER;
  Open(c);
  Decl sw;
  sw.d.layout.sizing.width = Fixed(Sc(44));
  sw.d.layout.sizing.height = Fixed(Sc(30));
  sw.d.backgroundColor = Col(KitRgb(n));
  sw.d.cornerRadius = {Sc(3), Sc(3), Sc(3), Sc(3)};
  sw.d.border = Border(KitRgb(0xffffff, 0.3f), 1, 1, 1, 1);
  Open(sw);
  Close();
  Text(pszLabel, UI_FACE_TEXT, Fs(11), 0.0f, kScInkMsg);
  Close();
}

// What the screen draws: the pick, the colour it lands as, and how readable the text stays.
void Readout(ThemeCustom *p)
{
  if (!g_bReported || g_reported.accent != p->accent || g_reported.accent2 != p->accent2 ||
      g_reported.panel != p->panel || g_reported.bLight != p->bLight) {
    g_report = ThemeReportOf(*p);
    g_reported = *p;
    g_bReported = true;
  }
  const unsigned int on = g_nSlot == 0 ? g_report.panel : g_nSlot == 1 ? g_report.accent : g_report.accent2;
  // {0} the colour the screen draws, in hex
  std::string t = PortTextF("On screen {0}.", {Hex(on)}) + " ";
  if (!p->bLight) {
    t += PortText("Lightness is the theme\xE2\x80\x99s: this pick gives the hue and how strong it is.");
  } else {
    char sz[32];
    SDL_snprintf(sz, sizeof sz, "%.1f:1", g_report.fMinText);
    // {0} how well the weakest text reads, as "4.5:1"; {1} how many colours were lifted
    if (g_report.nLifted > 0)
      t += PortTextNF("The weakest text on the panels reads at {0}; {1} colour was lifted to stay readable.",
                      "The weakest text on the panels reads at {0}; {1} colours were lifted to stay readable.",
                      g_report.nLifted, {sz, std::to_string(g_report.nLifted)});
    else
      t += PortTextF("The weakest text on the panels reads at {0}; nothing needed lifting.", {sz});
  }
  if (g_nSlot == 0 && p->bLight && ThemeLightOf(p->panel) > kThemePanelMax)
    t += std::string(" ") + PortText("Panels stay dark: this one is drawn at the lightest they allow.");
  Decl row;
  row.d.layout.sizing.width = Grow();
  row.d.layout.childGap = Gap(Fs(10));
  row.d.layout.childAlignment.y = CLAY_ALIGN_Y_CENTER;
  Open(row);
  OutSwatch(Colour(p, g_nSlot), PortText("You picked"));
  OutSwatch(on, PortText("On screen"));
  Decl words;
  words.d.layout.sizing.width = Grow();
  Open(words);
  Text(Keep(t), UI_FACE_TEXT, Fs(13), 0.0f, kScInkMsg, -1, 1.35f);
  Close();
  Close();
}

// A preset to start from: its three colours, the lightness left as it is.
void StartFrom(ThemeCustom *p, bool bLive)
{
  Decl row;
  row.d.layout.childGap = Gap(Fs(6));
  row.d.layout.childAlignment.y = CLAY_ALIGN_Y_CENTER;
  Open(row);
  RowLabel(PortText("Start from"));
  for (int k = 0; k < ThemePresets(); ++k) {
    Decl b;
    b.d.id = Id("cpfrom", k);
    const bool bHot = bLive && Over(b.d.id);
    const ThemeCustom t = ThemePreset(k);
    if (bLive && Clicked(b.d.id)) {
      p->accent = t.accent;
      p->accent2 = t.accent2;
      p->panel = t.panel;
      SyncHsv(p);
      g_nFocus = FOCUS_NONE;
    }
    b.d.layout.padding = Pad(Fs(3), Fs(3), Fs(3), Fs(3));
    b.d.custom.customData = Custom(DrawScRaised, TabLook(false, bHot));
    Open(b);
    Swatch(t, 10, 18);
    Close();
  }
  Close();
}

void Picker(ThemeCustom *p, bool bLive)
{
  g_bDrawn = true;
  SyncHsv(p);
  // a press anywhere but the entry being typed in leaves it
  if (g_bPressed && g_nFocus >= FOCUS_HEX && g_nFocus <= FOCUS_B && !Over(Id("cpentry", g_nFocus)))
    g_nFocus = FOCUS_NONE;
  if (g_bPressed && (g_nFocus == FOCUS_FIELD || g_nFocus == FOCUS_HUE) && !Over(Id("cpfield")) && !Over(Id("cphue")))
    g_nFocus = FOCUS_NONE;
  if (bLive)
    Typed(p);
  Decl box;
  box.d.layout.layoutDirection = CLAY_TOP_TO_BOTTOM;
  box.d.layout.sizing.width = Grow();
  box.d.layout.childGap = Gap(Fs(10));
  box.d.layout.padding = Pad(Fs(12), Fs(12), Fs(12), Fs(12));
  box.d.backgroundColor = Col(kScWell);
  box.d.border = Border(kScWellRim, 1, 1, 1, 1);
  box.d.cornerRadius = {Sc(4), Sc(4), Sc(4), Sc(4)};
  Open(box);
  Chips(p, bLive);
  Text(PortText(kSlotWhat[g_nSlot]), UI_FACE_TEXT, Fs(13), 0.0f, kScInkMsg);
  Pick(p, bLive);
  Values(p, bLive);
  Lightness(p, bLive);
  Readout(p);
  StartFrom(p, bLive);
  Close();
}

}  // namespace

void ThemePickerReset(void)
{
  g_nSlot = 1;
  g_nFocus = FOCUS_NONE;
  g_nDrag = FOCUS_NONE;
  g_entry.clear();
}

bool ThemePickerTyping(void)
{
  // a picker no longer shown (another tab, another theme) gives the keys back
  if (!g_bDrawn)
    g_nFocus = FOCUS_NONE;
  g_bDrawn = false;
  return g_nFocus >= FOCUS_HEX && g_nFocus <= FOCUS_B;
}

void ThemeGroup(int *pnTheme, ThemeCustom *pCustom, bool bLive)
{
  // the presets and Custom, two to a row (front.css .d-theme-grid)
  Decl grid;
  grid.d.layout.layoutDirection = CLAY_TOP_TO_BOTTOM;
  grid.d.layout.sizing.width = Grow();
  grid.d.layout.childGap = Gap(Fs(8));
  grid.d.layout.padding = Pad(0, 0, Fs(2), 0);
  Open(grid);
  const int nOptions = ThemePresets() + 1;
  for (int r = 0; r < nOptions; r += 2) {
    Decl row;
    row.d.layout.sizing.width = Grow();
    row.d.layout.childGap = Gap(Fs(8));
    Open(row);
    for (int k = r; k < r + 2 && k < nOptions; ++k) {
      Decl b;
      b.d.id = Id("thopt", k);
      const bool bOn = k == *pnTheme;
      const bool bHot = bLive && Over(b.d.id);
      if (bLive && Clicked(b.d.id) && !bOn) {
        *pnTheme = k;
        g_nFocus = FOCUS_NONE;
      }
      b.d.layout.sizing.width = Grow();
      b.d.layout.sizing.height = Fixed(Sc(44));
      b.d.layout.padding = Pad(Fs(10), Fs(10), 0, 0);
      b.d.layout.childGap = Gap(Fs(10));
      b.d.layout.childAlignment.y = CLAY_ALIGN_Y_CENTER;
      b.d.custom.customData = Custom(DrawScRaised, TabLook(bOn, bHot));
      Open(b);
      Swatch(k < ThemePresets() ? ThemePreset(k) : *pCustom, 16, 26);
      const char *pszName = k < ThemePresets() ? ThemePresetName(k) : PortText("Custom");
      Text(pszName, UI_FACE_TEXT, FitSize(b, pszName, UI_FACE_TEXT, Fs(16), 0.0f, Fs(10), Sc(16 * 3) + UiPx(Fs(10))),
           0.0f, bOn || bHot ? kScInkHi : kScInkButton);
      Close();
    }
    Close();
  }
  Close();
  if (*pnTheme == kCustomTheme)
    Picker(pCustom, bLive);
}

}  // namespace FrontKit
