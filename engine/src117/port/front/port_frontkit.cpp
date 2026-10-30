// port_frontkit.cpp -- the front end's kit (port_frontkit.h). Presentation only.
#include <windows.h>
#include <intrin.h>
#include <algorithm>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <SDL3/SDL.h>
#include "port_frontkit.h"
#include "port_frontscreens.h"  // FrontLog
#include "../platform/port_settings.h"    // the theme's settings

namespace FrontKit {

// The design's colours in the standard theme (port_frontkit.h); the theme writes them again.
UiColor kInk = KitRgb(0xd9e3ea);
UiColor kInkDim = KitRgb(0x8e9daa);
UiColor kInkFaint = KitRgb(0x5d6b78);
UiColor kGold = KitRgb(0xf2c94c);
UiColor kOk = KitRgb(0x4fd08a);
UiColor kTool = KitRgb(0x7fb8ff);
UiColor kPanelSolid = KitRgb(0x080b10);
UiColor kRule = {140 / 255.0f, 160 / 255.0f, 180 / 255.0f, 0.28f};
UiColor kGlass = {6 / 255.0f, 9 / 255.0f, 14 / 255.0f, 0.72f};
UiColor kGlassHi = {14 / 255.0f, 19 / 255.0f, 27 / 255.0f, 0.86f};
UiColor kGoldWash = {242 / 255.0f, 201 / 255.0f, 76 / 255.0f, 0.08f};
UiColor kGoldLine = {242 / 255.0f, 201 / 255.0f, 76 / 255.0f, 0.55f};
UiColor kNone = {0, 0, 0, 0};
UiColor kScInkHi = KitRgb(0xffffff);
UiColor kScGlow = KitRgb(0x0078ff);
UiColor kScInkButton = KitRgb(0x9eccf3);
UiColor kScInkHead = KitRgb(0xc6f0ff);
UiColor kScInkLabel = KitRgb(0x66d5fe);
UiColor kScInkMsg = KitRgb(0x3dabe6);
UiColor kScInkMenu = KitRgb(0x3a80bb);
UiColor kScInkTip = KitRgb(0x84d2ff);
UiColor kScInkMini = KitRgb(0x9fd1eb);
UiColor kScInkOff = KitRgb(0x39506d);
UiColor kScInkAccept = KitRgb(0xffe490);
UiColor kScBlackout = {0.0f, 0.0f, 0.0f, 0.71f};
UiColor kScFill = {1 / 255.0f, 9 / 255.0f, 24 / 255.0f, 0.9f};
UiColor kScFillSolid = {1 / 255.0f, 9 / 255.0f, 24 / 255.0f, 1.0f};
UiColor kScRim = KitRgb(0x498fda);
UiColor kScRimGlow = {0.0f, 92 / 255.0f, 206 / 255.0f, 0.45f};
UiColor kScBevel = {1 / 255.0f, 45 / 255.0f, 102 / 255.0f, 0.94f};
UiColor kScNotch = KitRgb(0x7bdbff);
UiColor kScWell = {0.0f, 0.0f, 0.0f, 0.17f};
UiColor kScWellRim = KitRgb(0x10344a);
UiColor kScBlack = {0.0f, 0.0f, 0.0f, 1.0f};
UiColor kScShade = {0.0f, 0.0f, 0.0f, 0.3f};
UiColor kScCheckTop = KitRgb(0x010814);
UiColor kScCheckBot = KitRgb(0x091d3a);
UiColor kScCheckRim = KitRgb(0x1d69c0);
UiColor kScCheckHi = KitRgb(0x001a41);
UiColor kScCheckRimHi = KitRgb(0x6bbbff);
UiColor kScDdRim = KitRgb(0x3780bc);
UiColor kScMenuRim = KitRgb(0x3190e9);
UiColor kScMenuFill = KitRgb(0x020910);
UiColor kScMenuSel = KitRgb(0x0b1633);
UiColor kScTrack = {0.0f, 0.0f, 0.0f, 0.83f};
UiColor kScTrackGlow = {0.0f, 120 / 255.0f, 1.0f, 0.23f};
UiColor kScSliderFill = KitRgb(0x54e6ff);
UiColor kScKnob = KitRgb(0x007ece);
UiColor kScKnobRim = KitRgb(0x0088de);
UiColor kScKnobHi = KitRgb(0x99f4ff);
UiColor kScKnobRimHi = KitRgb(0xbcf8ff);
UiColor kScTipFill = {0.0f, 9 / 255.0f, 24 / 255.0f, 0.95f};
UiColor kScTipRim = KitRgb(0x2982ce);
UiColor kScCountFill = {0.0f, 16 / 255.0f, 37 / 255.0f, 0.8f};
ScLook kScButton = {KitRgb(0x010910), KitRgb(0x07437a), KitRgb(0x4b95cf), KitRgb(0x0479bb),
                          {34 / 255.0f, 106 / 255.0f, 180 / 255.0f, 0.43f}, 6};
ScLook kScButtonHi = {KitRgb(0x001934), KitRgb(0x035aaa), KitRgb(0x9ed7ff), KitRgb(0x90d4f6),
                            {34 / 255.0f, 106 / 255.0f, 180 / 255.0f, 0.43f}, 6};
ScLook kScAccept = {KitRgb(0x270e01), KitRgb(0x7d3707), KitRgb(0xdaa308), KitRgb(0xc58301),
                          {122 / 255.0f, 63 / 255.0f, 24 / 255.0f, 0.76f}, 6};
ScLook kScAcceptHi = {KitRgb(0x391402), KitRgb(0x9d450a), KitRgb(0xfcdc6c), KitRgb(0xf4b601),
                            {122 / 255.0f, 63 / 255.0f, 24 / 255.0f, 0.76f}, 6};
ScLook kScTab = {KitRgb(0x010407), KitRgb(0x072b4f), KitRgb(0x3780bc), KitRgb(0x306fb0),
                       {47 / 255.0f, 75 / 255.0f, 103 / 255.0f, 0.52f}, 6};
ScLook kScTabHi = {KitRgb(0x001227), KitRgb(0x03407c), KitRgb(0x84c4f6), KitRgb(0x73baf4),
                         {47 / 255.0f, 75 / 255.0f, 103 / 255.0f, 0.52f}, 6};
ScLook kScTabOn = {KitRgb(0x000306), KitRgb(0x00142d), KitRgb(0xb3daff), KitRgb(0x0046b0),
                         {0.0f, 120 / 255.0f, 1.0f, 0.5f}, 6};
ScLook kScMini = {KitRgb(0x000c16), KitRgb(0x002b4f), KitRgb(0x006aa5), {0.0f, 18 / 255.0f, 42 / 255.0f, 0.9f},
                        {0.0f, 0.0f, 0.0f, 1.0f}, 3};
ScLook kScMiniHi = {KitRgb(0x000c16), KitRgb(0x002b4f), KitRgb(0x00d4ff), KitRgb(0x0480d5),
                          {0.0f, 0.0f, 0.0f, 1.0f}, 3};
ScLook kScDrop = {KitRgb(0x010305), KitRgb(0x09223c), KitRgb(0x3780bc), KitRgb(0x08375a),
                        {0.0f, 0.0f, 0.0f, 1.0f}, 5};
ScLook kScDropHi = {KitRgb(0x010305), KitRgb(0x09223c), KitRgb(0x9ed7ff), KitRgb(0x08375a),
                          {0.0f, 0.0f, 0.0f, 1.0f}, 5};
ScLook kScOff = {KitRgb(0x010910), KitRgb(0x010910), KitRgb(0x39506d), KitRgb(0x010910),
                       {0.0f, 0.0f, 0.0f, 0.0f}, 6};
UiColor kScInkNav = KitRgb(0x9aebff);
UiColor kScInkTipHead = KitRgb(0xc1ffff);
UiColor kScInkClock = KitRgb(0x538393);
UiColor kScIcon = KitRgb(0x5e96b6);
UiColor kScIconHi = KitRgb(0x9ed7ff);
UiColor kScBarUnder = {0.0f, 0.0f, 0.0f, 0.59f};
UiColor kScBarTop = {13 / 255.0f, 16 / 255.0f, 23 / 255.0f, 0.71f};
UiColor kScBarBot = {20 / 255.0f, 36 / 255.0f, 53 / 255.0f, 0.75f};
UiColor kScBarEdge = {25 / 255.0f, 65 / 255.0f, 85 / 255.0f, 0.79f};
UiColor kScSweep = {110 / 255.0f, 192 / 255.0f, 1.0f, 0.7f};
UiColor kScDividerTop = {43 / 255.0f, 173 / 255.0f, 210 / 255.0f, 0.1f};
UiColor kScDividerBot = {43 / 255.0f, 173 / 255.0f, 210 / 255.0f, 0.16f};
UiColor kScHoverLine = {40 / 255.0f, 118 / 255.0f, 210 / 255.0f, 0.3f};
UiColor kScHoverGlow = {0.0f, 107 / 255.0f, 240 / 255.0f, 0.12f};
UiColor kScSelGlow = {0.0f, 96 / 255.0f, 1.0f, 0.45f};
UiColor kScSelLine = KitRgb(0x4198e9);
UiColor kScSelSoft = {0.0f, 75 / 255.0f, 199 / 255.0f, 0.5f};
UiColor kScSelLine2 = KitRgb(0x9feaff);
UiColor kScAvaOuter = KitRgb(0x2776b3);
UiColor kScAvaMid = KitRgb(0x054273);
UiColor kScAvaInner = KitRgb(0x279fff);
UiColor kScAvaGlow = {0.0f, 138 / 255.0f, 1.0f, 0.35f};
UiColor kScAvaGlass = {1.0f, 1.0f, 1.0f, 0.43f};
UiColor kScAvaHover = {232 / 255.0f, 232 / 255.0f, 232 / 255.0f, 0.7f};
UiColor kScAvaHoverBand = {138 / 255.0f, 138 / 255.0f, 138 / 255.0f, 0.32f};
UiColor kScAvaFaceTop = KitRgb(0x0b2c4f);
UiColor kScAvaFaceBot = KitRgb(0x020a16);
UiColor kScPartyTop = {43 / 255.0f, 167 / 255.0f, 206 / 255.0f, 0.16f};
UiColor kScPartyFillTop = {46 / 255.0f, 112 / 255.0f, 168 / 255.0f, 0.02f};
UiColor kScPartyFillBot = {46 / 255.0f, 112 / 255.0f, 168 / 255.0f, 0.14f};
UiColor kScPartyBot = {44 / 255.0f, 145 / 255.0f, 191 / 255.0f, 0.27f};
UiColor kScCountRim = KitRgb(0x0060c2);
UiColor kScCountBevel = KitRgb(0x003c7c);
UiColor kScClockFill = {9 / 255.0f, 12 / 255.0f, 21 / 255.0f, 0.9f};
UiColor kScClockRim = KitRgb(0x192a3b);
UiColor kScToastTop = KitRgb(0x020b14);
UiColor kScToastBot = KitRgb(0x083662);
UiColor kScToastRim = KitRgb(0x2c6cae);
UiColor kScToastEdge = {81 / 255.0f, 118 / 255.0f, 165 / 255.0f, 0.55f};
UiColor kScToastShimmer = KitRgb(0xaac4ff);
UiColor kDShadow = {0.0f, 0.0f, 0.0f, 0.9f};
ScLook kScTask = {KitRgb(0x090d16), KitRgb(0x152d46), KitRgb(0x264461), KitRgb(0x284e71), {0.0f, 0.0f, 0.0f, 0.7f}, 6};
ScLook kScTaskHi = {KitRgb(0x090d17), KitRgb(0x063c6e), KitRgb(0x2f567a), KitRgb(0x1d6aad), {0.0f, 0.0f, 0.0f, 0.7f}, 6};
UiColor kLbInkInfo = KitRgb(0xcce7ff);
UiColor kLbInkDesc = KitRgb(0x8dbfea);
UiColor kLbInkEmpty = KitRgb(0x2c445b);
UiColor kLbInner = {4 / 255.0f, 18 / 255.0f, 36 / 255.0f, 0.62f};
UiColor kLbInnerRim = {39 / 255.0f, 96 / 255.0f, 150 / 255.0f, 0.45f};
UiColor kLbItem = {8 / 255.0f, 30 / 255.0f, 56 / 255.0f, 0.55f};
UiColor kLbItemHi = {18 / 255.0f, 64 / 255.0f, 112 / 255.0f, 0.6f};
UiColor kLbItemRim = {57 / 255.0f, 128 / 255.0f, 188 / 255.0f, 0.35f};
UiColor kLbDivider = KitRgb(0x0f2a45);
UiColor kLbHyperionRim = {218 / 255.0f, 163 / 255.0f, 8 / 255.0f, 0.7f};
UiColor kLbWarn = KitRgb(0xffb347);

float g_mouseX = -1.0f;
float g_mouseY = -1.0f;
bool g_bDown;
bool g_bPressed;          // this frame
bool g_bReleased;
uint32_t g_nPressedOn;    // the element the button went down on
std::vector<int> g_keys;  // the frame's key presses (virtual keys)
std::string g_typed;
std::deque<std::string> g_strings;  // the frame's text, alive (and unmoved) until its layout is drawn

// ---------------------------------------------------------------------------
// Clay, without its macros: the engine is C++14 and they need C++20.

Clay_String Str(const char *psz, int nLength)
{
  Clay_String s;
  if (!psz) {
    psz = "";
    nLength = 0;
  }
  s.isStaticallyAllocated = false;
  s.length = nLength < 0 ? static_cast<int32_t>(strlen(psz)) : nLength;
  s.chars = psz;
  return s;
}

// Text that lives until the layout is drawn.
const char *Keep(const std::string &s)
{
  g_strings.push_back(s);
  return g_strings.back().c_str();
}

Clay_ElementId Id(const char *psz, int nIndex)
{
  return Clay__HashString(Str(psz), static_cast<uint32_t>(nIndex), 0);
}

Clay_SizingAxis Fixed(float f)
{
  Clay_SizingAxis a;
  SDL_zero(a);
  a.size.minMax.min = f;
  a.size.minMax.max = f;
  a.type = CLAY__SIZING_TYPE_FIXED;
  return a;
}

Clay_SizingAxis Grow(void)
{
  Clay_SizingAxis a;
  SDL_zero(a);
  a.type = CLAY__SIZING_TYPE_GROW;
  return a;
}

Clay_SizingAxis Fit(float fMax)
{
  Clay_SizingAxis a;
  SDL_zero(a);
  a.size.minMax.max = fMax;
  a.type = CLAY__SIZING_TYPE_FIT;
  return a;
}

Clay_Color Col(UiColor c)
{
  Clay_Color k;
  k.r = c.r * 255.0f;
  k.g = c.g * 255.0f;
  k.b = c.b * 255.0f;
  k.a = c.a * 255.0f;
  return k;
}

Clay_Padding Pad(float l, float r, float t, float b)
{
  Clay_Padding p;
  p.left = static_cast<uint16_t>(UiPx(l) + 0.5f);
  p.right = static_cast<uint16_t>(UiPx(r) + 0.5f);
  p.top = static_cast<uint16_t>(UiPx(t) + 0.5f);
  p.bottom = static_cast<uint16_t>(UiPx(b) + 0.5f);
  return p;
}

uint16_t Gap(float f)
{
  return static_cast<uint16_t>(UiPx(f) + 0.5f);
}

Clay_BorderElementConfig Border(UiColor c, float l, float r, float t, float b)
{
  Clay_BorderElementConfig k;
  SDL_zero(k);
  k.color = Col(c);
  k.width.left = static_cast<uint16_t>(l > 0.0f ? fmaxf(1.0f, roundf(UiPx(l))) : 0.0f);
  k.width.right = static_cast<uint16_t>(r > 0.0f ? fmaxf(1.0f, roundf(UiPx(r))) : 0.0f);
  k.width.top = static_cast<uint16_t>(t > 0.0f ? fmaxf(1.0f, roundf(UiPx(t))) : 0.0f);
  k.width.bottom = static_cast<uint16_t>(b > 0.0f ? fmaxf(1.0f, roundf(UiPx(b))) : 0.0f);
  return k;
}

// The frame's open elements, so that a Close with none of them open (which would close
// Clay's root and leave its children buffer short) is refused and named in the log.
int g_nDepth;

void LayoutCheckBegin(void)
{
  g_nDepth = 0;
}

void LayoutCheckEnd(const char *pszWhere)
{
  if (g_nDepth != 0)
    FrontLog("layout: %s left %d element%s open\n", pszWhere, g_nDepth, g_nDepth == 1 ? "" : "s");
}

void Open(const Decl &x)
{
  ++g_nDepth;
  Clay__OpenElement();
  Clay__ConfigureOpenElement(x.d);
}

__declspec(noinline) void Close(void)
{
  if (g_nDepth <= 0) {
    // once for each place, as a frame repeats it
    static std::vector<uintptr_t> s_logged;
    const uintptr_t at = reinterpret_cast<uintptr_t>(_ReturnAddress());
    if (std::find(s_logged.begin(), s_logged.end(), at) == s_logged.end()) {
      s_logged.push_back(at);
      const uintptr_t base = reinterpret_cast<uintptr_t>(GetModuleHandleW(NULL));
      FrontLog("layout: a Close with nothing open, from Hyperion.exe+0x%llX\n",
               static_cast<unsigned long long>(at - base));
    }
    return;
  }
  --g_nDepth;
  Clay__CloseElement();
}

// A run of text. Sizes and letter-spacing are the design's units (CSS's em spacing times the
// size); nUnderline marks a hotkey's letter, and bCenter centres wrapped lines.
namespace {
void TextWith(const char *psz, UiFace face, float fSize, float fSpacingEm, UiColor c, int nUnderline,
              float fLineHeight, bool bCenter, int nFlags)
{
  Clay_TextElementConfig k;
  SDL_zero(k);
  k.userData = reinterpret_cast<void *>(static_cast<intptr_t>((nUnderline + 1) | nFlags));
  k.textColor = Col(c);
  k.fontId = static_cast<uint16_t>(face);
  k.fontSize = static_cast<uint16_t>(roundf(UiPx(fSize)));
  k.letterSpacing = static_cast<uint16_t>(roundf(UiPx(fSize * fSpacingEm)));
  k.lineHeight = static_cast<uint16_t>(fLineHeight > 0.0f ? roundf(UiPx(fSize * fLineHeight)) : 0.0f);
  k.wrapMode = CLAY_TEXT_WRAP_WORDS;
  k.textAlignment = bCenter ? CLAY_TEXT_ALIGN_CENTER : CLAY_TEXT_ALIGN_LEFT;
  Clay__OpenTextElement(Str(psz), Clay__StoreTextElementConfig(k));
}
}  // namespace

void Text(const char *psz, UiFace face, float fSize, float fSpacingEm, UiColor c, int nUnderline,
          float fLineHeight, bool bCenter)
{
  TextWith(psz, face, fSize, fSpacingEm, c, nUnderline, fLineHeight, bCenter, 0);
}

void TextGlow(const char *psz, UiFace face, float fSize, float fSpacingEm, UiColor c, int nUnderline,
              float fLineHeight, bool bCenter)
{
  TextWith(psz, face, fSize, fSpacingEm, c, nUnderline, fLineHeight, bCenter, UI_TEXT_GLOW);
}

std::string Upper(const char *psz)
{
  return UiUpper(psz);
}

std::string FileDate(const FILETIME &ft)
{
  FILETIME local;
  SYSTEMTIME st;
  FileTimeToLocalFileTime(&ft, &local);
  FileTimeToSystemTime(&local, &st);
  char szTime[16];
  snprintf(szTime, sizeof szTime, "%02u:%02u", st.wHour, st.wMinute);
  // day, month, year and time, in the language's order
  return PortTextF("{0} {1} {2}, {3}", {std::to_string(st.wDay), PortTextC("month", kMonths[(st.wMonth + 11) % 12]),
                                        std::to_string(st.wYear), szTime});
}

std::string Thousands(long long n)
{
  // the language's mark between thousands: a comma in English, a point in German, a space in French
  const std::string mark = PortTextC("thousands", ",");
  std::string s = std::to_string(n < 0 ? -n : n);
  for (int i = static_cast<int>(s.size()) - 3; i > 0; i -= 3)
    s.insert(static_cast<size_t>(i), mark);
  return n < 0 ? "-" + s : s;
}

std::string Elide(const std::string &s, UiFace face, float fSize, float fWidth)
{
  const float fPx = roundf(UiPx(fSize));
  if (UiTextWidth(face, fPx, 0.0f, s.c_str()) <= fWidth)
    return s;
  std::string t = s;
  while (!t.empty()) {
    while (!t.empty() && (static_cast<unsigned char>(t.back()) & 0xC0) == 0x80)
      t.pop_back();
    if (!t.empty())
      t.pop_back();
    if (UiTextWidth(face, fPx, 0.0f, (t + "\xe2\x80\xa6").c_str()) <= fWidth)
      return t + "\xe2\x80\xa6";
  }
  return t;
}

const char *GameTypeName(int n)
{
  switch (n) {
  case 0x02: return PortText("Melee");
  case 0x03: return PortText("Free For All");
  case 0x04: return PortText("One on One");
  case 0x05: return PortText("Capture The Flag");
  case 0x06: return PortText("Greed");
  case 0x07: return PortText("Slaughter");
  case 0x08: return PortText("Sudden Death");
  case 0x09: return PortText("Ladder");
  case 0x0A: return PortText("Use Map Settings");
  case 0x0B: return PortText("Team Melee");
  case 0x0C: return PortText("Team Free For All");
  case 0x0D: return PortText("Team Capture The Flag");
  case 0x0F: return PortText("Top vs Bottom");
  default: return PortText("Custom");
  }
}

// The pointer is over the element (as the last frame laid it out).
bool Over(Clay_ElementId id)
{
  return Clay_PointerOver(id);
}

// The element was clicked: the button went down and came up on it.
bool Clicked(Clay_ElementId id)
{
  if (g_bPressed && Over(id))
    g_nPressedOn = id.id;
  return g_bReleased && g_nPressedOn == id.id && Over(id);
}

bool DoubleClicked(Clay_ElementId id)
{
  static uint32_t s_nLast;  // the element clicked last; 0 none, after a double click
  static Uint64 s_nAt;
  const Uint64 nNow = SDL_GetTicks();
  const bool bDouble = s_nLast == id.id && nNow - s_nAt < 450;
  s_nLast = bDouble ? 0 : id.id;
  s_nAt = nNow;
  return bDouble;
}

bool Key(int nVk)
{
  for (int k : g_keys)
    if (k == nVk)
      return true;
  return false;
}

int g_nWheel;  // the frame's wheel turns, 120 a notch, up positive
std::deque<ScDraw> g_draws;  // Custom's
std::deque<UiCustom> g_customs;

UiCustom *Custom(void (*pfnDraw)(float, float, float, float, void *), const ScLook &look, float f, int n)
{
  ScDraw d = {look, f, n};
  g_draws.push_back(d);
  UiCustom c = {0.0f, pfnDraw, &g_draws.back()};
  g_customs.push_back(c);
  return &g_customs.back();
}

// SC2's units: its screens are 1200 high, the stage 1080.
float Sc(float f)
{
  return UiPx(Fs(f));
}

float Fs(float f)
{
  return f * 0.9f;
}

void Spacer(void)
{
  Decl s;
  s.d.layout.sizing.width = Grow();
  Open(s);
  Close();
}

Decl Box(const char *pszId, int nIndex)
{
  Decl b;
  if (pszId != NULL)
    b.d.id = Id(pszId, nIndex);
  return b;
}

void DialogTitle(const char *psz)
{
  Decl t;
  t.d.layout.sizing.width = Grow();
  t.d.layout.childAlignment.x = CLAY_ALIGN_X_CENTER;
  Open(t);
  Text(psz, UI_FACE_WIDE, Fs(24), 0.04f, kScInkHi);
  Close();
}

int WheelRows(bool bOver, int nPerNotch)
{
  static int s_nPart;  // turns short of a row, kept for the next
  if (!bOver || g_nWheel == 0)
    return 0;
  const int n = s_nPart - g_nWheel * nPerNotch;
  s_nPart = n % 120;
  return n / 120;
}

float WheelPixels(bool bOver, float fPerNotch)
{
  return bOver ? -g_nWheel / 120.0f * fPerNotch : 0.0f;
}

std::vector<std::string> Words(const char *psz)
{
  std::vector<std::string> v;
  if (psz == NULL)
    return v;
  const char *p = psz;
  for (;;) {
    const char *q = strchr(p, '|');
    if (q == NULL) {
      v.push_back(p);
      break;
    }
    v.push_back(std::string(p, q - p));
    p = q + 1;
  }
  return v;
}

// Where a hotkey's letter is in a label, counted in characters as UiText's underline counts
// them, or -1 (a translated label may not have it).
int KeyAt(const char *psz, char cKey)
{
  int nChar = 0;
  for (const char *p = psz; *p; ++p) {
    if ((static_cast<unsigned char>(*p) & 0xC0) == 0x80)
      continue;
    if (*p == cKey || *p == cKey - 'A' + 'a')
      return nChar;
    ++nChar;
  }
  return -1;
}

// ---------------------------------------------------------------------------
// SC2's controls, drawn.

// The dialog's frame (pageassets_dialogstandardbg): navy, a rim glowing outside with a bevel
// inside, and a lit notch at the middle of the top and the foot.
// pCtx, when set, is the fill (a UiColor): kScFillSolid for a modal dialog.
void DrawScFrame(float x, float y, float w, float h, void *pCtx)
{
  UiRing(x - Sc(6), y - Sc(6), w + Sc(12), h + Sc(12), {kScRimGlow.r, kScRimGlow.g, kScRimGlow.b, 0.18f}, Sc(10), Sc(6));
  UiRing(x - Sc(2), y - Sc(2), w + Sc(4), h + Sc(4), kScRimGlow, Sc(6), Sc(3));
  UiFill(x, y, w, h, pCtx != NULL ? *static_cast<const UiColor *>(pCtx) : kScFill, Sc(4));
  UiRing(x + Sc(2), y + Sc(2), w - Sc(4), h - Sc(4), {kScBevel.r, kScBevel.g, kScBevel.b, 0.6f}, Sc(3), Sc(5));
  UiRing(x + Sc(2), y + Sc(2), w - Sc(4), h - Sc(4), kScBlack, Sc(3), fmaxf(1.0f, Sc(1)));
  UiRing(x, y, w, h, kScRim, Sc(4), Sc(2));
  for (int i = 0; i < 2; ++i) {
    const float ny = i == 0 ? y - Sc(2) : y + h - Sc(3);
    UiFill(x + w / 2 - Sc(66), ny - Sc(3), Sc(132), Sc(11), {kScGlow.r, kScGlow.g, kScGlow.b, 0.35f}, Sc(5));
    UiFill(x + w / 2 - Sc(58), ny, Sc(116), Sc(5), kScNotch, Sc(1));
  }
}

// A raised control: the large button, the tab, the mini button and the pulldown's face.
void DrawScRaised(float x, float y, float w, float h, void *pCtx)
{
  const ScDraw &d = *static_cast<const ScDraw *>(pCtx);
  const ScLook &k = d.look;
  const float r = Sc(k.fRadius);
  if (k.halo.a > 0.0f)
    UiRing(x - Sc(3), y - Sc(3), w + Sc(6), h + Sc(6), k.halo, r + Sc(3), Sc(3));
  UiGradient(x, y, w, h, k.top, k.bot, false, r);
  UiFill(x + Sc(2), y + h - Sc(2) - Sc(4), w - Sc(4), Sc(4), k.lip, Sc(1));
  UiRing(x + Sc(2), y + Sc(2), w - Sc(4), h - Sc(4), kScBlack, fmaxf(0.0f, r - Sc(2)), Sc(2));
  UiRing(x, y, w, h, k.rim, r, Sc(2));
  // the pulldown's arrow: two chevrons glowing blue (n 1 down, 2 up)
  if (d.n != 0) {
    const float cx = x + w - Sc(12) - Sc(12.5f);
    const float cy = y + h / 2;
    const float s = d.n == 2 ? -1.0f : 1.0f;
    for (int i = 0; i < 2; ++i) {
      const float ty = cy + s * (i == 0 ? -Sc(5) : Sc(3));
      for (int g = 0; g < 2; ++g) {
        const UiColor c = g == 0 ? UiColor{kScGlow.r, kScGlow.g, kScGlow.b, 0.5f} : kScInkHi;
        const float wd = g == 0 ? Sc(6) : Sc(3);
        UiLine(cx - Sc(6), ty - s * Sc(3), cx, ty + s * Sc(3), c, wd);
        UiLine(cx, ty + s * Sc(3), cx + Sc(6), ty - s * Sc(3), c, wd);
      }
    }
  }
}

// The check box (bluecheckbox), or a choice's round one: n bit 1 checked, bit 2 under the
// pointer, bit 4 round, bit 8 disabled.
void DrawScCheck(float x, float y, float w, float h, void *pCtx)
{
  const ScDraw &d = *static_cast<const ScDraw *>(pCtx);
  const bool bOn = (d.n & 1) != 0, bHot = (d.n & 2) != 0, bRound = (d.n & 4) != 0, bOff = (d.n & 8) != 0;
  const float r = bRound ? fminf(w, h) / 2 : Sc(6);
  UiRing(x - Sc(2), y - Sc(2), w + Sc(4), h + Sc(4), kScShade, r + Sc(2), Sc(2));
  if (bHot && !bOn)
    UiFill(x, y, w, h, kScCheckHi, r);
  else
    UiGradient(x, y, w, h, kScCheckTop, kScCheckBot, false, r);
  UiRing(x, y, w, h, bOff ? kScInkOff : bHot ? kScCheckRimHi : kScCheckRim, r, Sc(2));
  if (!bOn)
    return;
  const UiColor mark = bOff ? kScInkOff : kScInkHi;
  if (bRound) {
    const float cx = x + w / 2, cy = y + h / 2;
    if (!bOff)
      UiFill(cx - w * 0.34f, cy - w * 0.34f, w * 0.68f, w * 0.68f, {kScGlow.r, kScGlow.g, kScGlow.b, 0.8f}, w * 0.34f);
    UiFill(cx - w * 0.2f, cy - w * 0.2f, w * 0.4f, w * 0.4f, mark, w * 0.2f);
    return;
  }
  // the tick, glowing blue
  const float x0 = x + w * 0.24f, y0 = y + h * 0.52f, x1 = x + w * 0.43f, y1 = y + h * 0.72f, x2 = x + w * 0.76f,
              y2 = y + h * 0.26f;
  if (!bOff) {
    const UiColor glow = {kScGlow.r, kScGlow.g, kScGlow.b, 0.55f};
    UiLine(x0, y0, x1, y1, glow, Sc(8));
    UiLine(x1, y1, x2, y2, glow, Sc(8));
  }
  UiLine(x0, y0, x1, y1, mark, Sc(3));
  UiLine(x1, y1, x2, y2, mark, Sc(3));
}

// The slider (sliderframe, sliderfill, sliderhandle): f where the handle is, n bit 1 under the
// pointer, bit 2 disabled. The handle's range is inset 9 units each side.
void DrawScSlider(float x, float y, float w, float h, void *pCtx)
{
  const ScDraw &d = *static_cast<const ScDraw *>(pCtx);
  const bool bHot = (d.n & 1) != 0, bOff = (d.n & 2) != 0;
  const float ty = y + Sc(12);
  if (!bOff)
    UiFill(x - Sc(1), ty - Sc(1), w + Sc(2), Sc(9), kScTrackGlow, Sc(2));
  UiFill(x, ty, w, Sc(7), kScTrack);
  const float cx = x + Sc(9) + d.f * (w - Sc(18));
  if (!bOff)
    UiFill(x + Sc(2), ty + Sc(2), cx - x - Sc(2), Sc(3), kScSliderFill);
  const UiColor body = bOff ? kScInkOff : bHot ? kScKnobHi : kScKnob;
  const UiColor rim = bOff ? kScInkOff : bHot ? kScKnobRimHi : kScKnobRim;
  // pointed at the top: a turned square under the body's shoulders
  UiSetTurn(3.14159265f / 4, cx, y + Sc(9));
  UiFill(cx - Sc(6.4f), y + Sc(9) - Sc(6.4f), Sc(12.8f), Sc(12.8f), body);
  UiSetTurn(0.0f, 0.0f, 0.0f);
  UiFill(cx - Sc(9), y + Sc(9), Sc(18), Sc(23), body);
  UiFill(cx - Sc(9), y + Sc(9), Sc(2), Sc(23), rim);
  UiFill(cx + Sc(7), y + Sc(9), Sc(2), Sc(23), rim);
}

// The ? beside a setting (ui_glues_social_counter's round).
void DrawScQuery(float x, float y, float w, float h, void *pCtx)
{
  const ScDraw &d = *static_cast<const ScDraw *>(pCtx);
  UiFill(x, y, w, h, kScCountFill, w / 2);
  UiRing(x, y, w, h, d.n ? kScButtonHi.rim : kScDdRim, w / 2, Sc(2));
}

// A plain box: the tip, the pulldown's menu, the key box and the path field.
void DrawScBox(float x, float y, float w, float h, void *pCtx)
{
  const ScDraw &d = *static_cast<const ScDraw *>(pCtx);
  const ScLook &k = d.look;
  if (k.halo.a > 0.0f)
    UiRing(x - Sc(2), y - Sc(2), w + Sc(4), h + Sc(4), k.halo, Sc(k.fRadius) + Sc(2), Sc(2));
  UiGradient(x, y, w, h, k.top, k.bot, false, Sc(k.fRadius));
  UiRing(x, y, w, h, k.rim, Sc(k.fRadius), Sc(2));
}

// ---------------------------------------------------------------------------
// The dialog, laid out.

Decl Floating(const char *pszId, int nIndex, float x, float y, int nZ, Clay_FloatingAttachPointType element,
              Clay_FloatingAttachPointType parent)
{
  Decl x_;
  if (pszId != NULL)
    x_.d.id = Id(pszId, nIndex);
  x_.d.floating.attachTo = CLAY_ATTACH_TO_PARENT;
  x_.d.floating.attachPoints.element = element;
  x_.d.floating.attachPoints.parent = parent;
  x_.d.floating.offset.x = x;
  x_.d.floating.offset.y = y;
  x_.d.floating.zIndex = static_cast<int16_t>(nZ);
  return x_;
}

float FitSize(const Decl &b, const char *psz, UiFace face, float fSize, float fSpacingEm, float fPad, float fBeside)
{
  float fWidth;
  if (b.d.layout.sizing.width.type == CLAY__SIZING_TYPE_FIXED) {
    fWidth = b.d.layout.sizing.width.size.minMax.min;
  } else {
    // a button that grows: its width at the last frame
    const Clay_ElementData e = Clay_GetElementData(b.d.id);
    if (!e.found)
      return fSize;
    fWidth = e.boundingBox.width;
  }
  const float fRoom = fWidth - 2.0f * UiPx(fPad) - fBeside;
  const float fLeast = fSize * 0.6f;
  while (fSize > fLeast && UiTextWidth(face, roundf(UiPx(fSize)), roundf(UiPx(fSize * fSpacingEm)), psz) > fRoom)
    fSize *= 0.95f;
  return fSize;
}

// A large button (SC2's BattlenetStandardButton, or the gold BattlenetAlternateButton), its
// label upper case with the hotkey's letter underlined; true when clicked.
bool ScButton(Decl b, const char *pszLabel, char cKey, bool bGold, bool bEnabled)
{
  const bool bHot = bEnabled && Over(b.d.id);
  const bool bClicked = bEnabled && Clicked(b.d.id);
  b.d.layout.childAlignment.x = CLAY_ALIGN_X_CENTER;
  b.d.layout.childAlignment.y = CLAY_ALIGN_Y_CENTER;
  b.d.layout.padding = Pad(Fs(24), Fs(24), 0, 0);
  b.d.custom.customData =
      Custom(DrawScRaised, !bEnabled ? kScOff : bGold ? (bHot ? kScAcceptHi : kScAccept) : bHot ? kScButtonHi : kScButton);
  Open(b);
  const std::string label = Upper(pszLabel);
  const UiColor c = !bEnabled ? kScInkOff : bHot ? kScInkHi : bGold ? kScInkAccept : kScInkButton;
  Text(Keep(label), UI_FACE_WIDE, FitSize(b, label.c_str(), UI_FACE_WIDE, Fs(17), 0.04f, Fs(24)), 0.04f, c,
       bEnabled && cKey ? KeyAt(label.c_str(), cKey) : -1);
  Close();
  return bClicked;
}

bool ScCheck(Decl b, const char *pszLabel, char cKey, bool bOn, bool bEnabled)
{
  const bool bClicked = bEnabled && Clicked(b.d.id);
  const bool bHot = bEnabled && Over(b.d.id);
  b.d.layout.sizing.height = Fixed(Sc(40));
  b.d.layout.childGap = Gap(Fs(12));
  b.d.layout.childAlignment.y = CLAY_ALIGN_Y_CENTER;
  Open(b);
  Decl check;
  check.d.layout.sizing.width = Fixed(Sc(26));
  check.d.layout.sizing.height = Fixed(Sc(26));
  check.d.custom.customData = Custom(DrawScCheck, kScButton, 0.0f, (bOn ? 1 : 0) | (bHot ? 2 : 0) | (bEnabled ? 0 : 8));
  Open(check);
  Close();
  Text(pszLabel, UI_FACE_BOLD, Fs(16), 0.0f, bEnabled ? kScInkLabel : kScInkOff, bEnabled && cKey ? KeyAt(pszLabel, cKey) : -1);
  Close();
  return bClicked;
}

// The edit box's face (front.css .sc-edit): the dropdown's rim on a dark well, lit while it
// has the keyboard.
void DrawScEdit(float x, float y, float w, float h, void *pCtx)
{
  const bool bFocus = static_cast<const ScDraw *>(pCtx)->n != 0;
  UiFill(x, y, w, h, kScWell, Sc(4));
  if (bFocus)
    UiRing(x - Sc(3), y - Sc(3), w + Sc(6), h + Sc(6), {kScGlow.r, kScGlow.g, kScGlow.b, 0.35f}, Sc(7), Sc(3));
  UiRing(x, y, w, h, bFocus ? kScTabHi.rim : kScDdRim, Sc(4), Sc(2));
}

void ScEdit(Decl b, std::string *pText, size_t nMax, const char *pszPlaceholder, bool bFocus)
{
  if (bFocus) {
    // the typing is this box's: taken, so no other box drawn after it has it too; a character
    // of several bytes goes in whole or not at all, and Backspace takes it whole
    for (size_t i = 0; i < g_typed.size();) {
      size_t n = 1;
      while (i + n < g_typed.size() && (static_cast<unsigned char>(g_typed[i + n]) & 0xC0) == 0x80)
        ++n;
      if (pText->size() + n <= nMax)
        pText->append(g_typed, i, n);
      i += n;
    }
    g_typed.clear();
    if (Key(VK_BACK) && !pText->empty()) {
      while (pText->size() > 1 && (static_cast<unsigned char>(pText->back()) & 0xC0) == 0x80)
        pText->pop_back();
      pText->pop_back();
    }
    g_keys.erase(std::remove(g_keys.begin(), g_keys.end(), VK_BACK), g_keys.end());
  }
  b.d.layout.childAlignment.y = CLAY_ALIGN_Y_CENTER;
  b.d.layout.padding = Pad(Fs(16), Fs(16), 0, 0);
  b.d.custom.customData = Custom(DrawScEdit, kScButton, 0.0f, bFocus ? 1 : 0);
  Open(b);
  if (pText->empty()) {
    Text(pszPlaceholder != NULL ? pszPlaceholder : "", UI_FACE_TEXT, Fs(18), 0.0f, kScInkOff);
  } else {
    Text(Keep(*pText), UI_FACE_TEXT, Fs(18), 0.0f, kScInkHi);
  }
  // the caret, blinking every half second
  if (bFocus && (SDL_GetTicks() / 530) % 2 == 0) {
    Decl caret;
    caret.d.layout.sizing.width = Fixed(fmaxf(1.0f, Sc(2)));
    caret.d.layout.sizing.height = Fixed(Sc(24));
    caret.d.backgroundColor = Col(kScInkHi);
    Open(caret);
    Close();
  }
  Close();
}

// The mini button (StandardButtonMini).
bool ScMini(Decl b, const char *pszLabel, char cKey, bool bEnabled)
{
  const bool bHot = bEnabled && Over(b.d.id);
  const bool bClicked = bEnabled && Clicked(b.d.id);
  b.d.layout.childAlignment.x = CLAY_ALIGN_X_CENTER;
  b.d.layout.childAlignment.y = CLAY_ALIGN_Y_CENTER;
  b.d.layout.padding = Pad(Fs(22), Fs(22), 0, 0);
  b.d.custom.customData = Custom(DrawScRaised, !bEnabled ? kScOff : bHot ? kScMiniHi : kScMini);
  Open(b);
  Text(pszLabel, UI_FACE_TEXT, FitSize(b, pszLabel, UI_FACE_TEXT, Fs(16), 0.0f, Fs(22)), 0.0f,
       !bEnabled ? kScInkOff : bHot ? kScInkHi : kScInkMini, bEnabled && cKey ? KeyAt(pszLabel, cKey) : -1);
  Close();
  return bClicked;
}

bool ScMiniTab(Decl b, const char *pszLabel, char cKey, bool bOn)
{
  b.d.layout.sizing.width = Fit();
  b.d.layout.sizing.width.size.minMax.min = Sc(170);
  b.d.layout.sizing.height = Fixed(Sc(42));
  if (bOn)
    b.d.border = Border(kLbHyperionRim, Fs(2), Fs(2), Fs(2), Fs(2));
  return ScMini(b, pszLabel, cKey);
}

// ---------------------------------------------------------------------------
// Settings' theme (port_frontkit.h). Every colour of the kit but kNone and kOk, whose green
// says a thing is ready, kept as the standard theme has it and turned from there.

namespace {

UiColor *const kThemed[] = {
    &kScInkNav, &kScInkTipHead, &kScInkClock, &kScIcon, &kScIconHi, &kScBarUnder, &kScBarTop, &kScBarBot,
    &kScBarEdge, &kScSweep, &kScDividerTop, &kScDividerBot, &kScHoverLine, &kScHoverGlow, &kScSelGlow,
    &kScSelLine, &kScSelSoft, &kScSelLine2, &kScAvaOuter, &kScAvaMid, &kScAvaInner, &kScAvaGlow,
    &kScAvaGlass, &kScAvaHover, &kScAvaHoverBand, &kScAvaFaceTop, &kScAvaFaceBot, &kScPartyTop,
    &kScPartyFillTop, &kScPartyFillBot, &kScPartyBot, &kScCountRim, &kScCountBevel, &kScClockFill,
    &kScClockRim, &kScToastTop, &kScToastBot, &kScToastRim, &kScToastEdge, &kScToastShimmer, &kDShadow,
    &kScTask.top, &kScTask.bot, &kScTask.rim, &kScTask.lip, &kScTask.halo, &kScTaskHi.top, &kScTaskHi.bot,
    &kScTaskHi.rim, &kScTaskHi.lip, &kScTaskHi.halo, &kLbInkInfo, &kLbInkDesc, &kLbInkEmpty, &kLbInner,
    &kLbInnerRim, &kLbItem, &kLbItemHi, &kLbItemRim, &kLbDivider, &kLbHyperionRim,
    &kInk, &kInkDim, &kInkFaint, &kGold, &kTool, &kPanelSolid, &kRule, &kGlass, &kGlassHi, &kGoldWash,
    &kGoldLine, &kScInkHi, &kScGlow, &kScInkButton, &kScInkHead, &kScInkLabel, &kScInkMsg, &kScInkMenu,
    &kScInkTip, &kScInkMini, &kScInkOff, &kScInkAccept, &kScBlackout, &kScFill, &kScFillSolid, &kScRim,
    &kScRimGlow, &kScBevel, &kScNotch, &kScWell, &kScWellRim, &kScBlack, &kScShade, &kScCheckTop,
    &kScCheckBot, &kScCheckRim, &kScCheckHi, &kScCheckRimHi, &kScDdRim, &kScMenuRim, &kScMenuFill,
    &kScMenuSel, &kScTrack, &kScTrackGlow, &kScSliderFill, &kScKnob, &kScKnobRim, &kScKnobHi, &kScKnobRimHi,
    &kScTipFill, &kScTipRim, &kScCountFill, &kScButton.top, &kScButton.bot, &kScButton.rim, &kScButton.lip,
    &kScButton.halo, &kScButtonHi.top, &kScButtonHi.bot, &kScButtonHi.rim, &kScButtonHi.lip,
    &kScButtonHi.halo, &kScAccept.top, &kScAccept.bot, &kScAccept.rim, &kScAccept.lip, &kScAccept.halo,
    &kScAcceptHi.top, &kScAcceptHi.bot, &kScAcceptHi.rim, &kScAcceptHi.lip, &kScAcceptHi.halo, &kScTab.top,
    &kScTab.bot, &kScTab.rim, &kScTab.lip, &kScTab.halo, &kScTabHi.top, &kScTabHi.bot, &kScTabHi.rim,
    &kScTabHi.lip, &kScTabHi.halo, &kScTabOn.top, &kScTabOn.bot, &kScTabOn.rim, &kScTabOn.lip,
    &kScTabOn.halo, &kScMini.top, &kScMini.bot, &kScMini.rim, &kScMini.lip, &kScMini.halo, &kScMiniHi.top,
    &kScMiniHi.bot, &kScMiniHi.rim, &kScMiniHi.lip, &kScMiniHi.halo, &kScDrop.top, &kScDrop.bot,
    &kScDrop.rim, &kScDrop.lip, &kScDrop.halo, &kScDropHi.top, &kScDropHi.bot, &kScDropHi.rim,
    &kScDropHi.lip, &kScDropHi.halo, &kScOff.top, &kScOff.bot, &kScOff.rim, &kScOff.lip, &kScOff.halo};
const int kThemedCount = sizeof kThemed / sizeof kThemed[0];
UiColor g_standard[kThemedCount];
bool g_bStandardKept;

// A theme is three colours: the accent, the standard's blue; the
// second accent, its gold; and the panels, the near-black under everything. The presets in
// Settings' order, the standard first.
const ThemeCustom kThemes[] = {
    {0x498fda, 0xdaa308, 0x010918, false},  // blue and gold
    {0xe0453a, 0xe2b36a, 0x0c0506, false},  // black and red
    {0x9b6bff, 0x4fdc84, 0x0c0718, false},  // purple and green
    {0x9fb0c2, 0xf08c2a, 0x090b0e, false},  // steel and amber
    {0xd9a83a, 0x3fd0e0, 0x0d0a04, false},  // gold and cyan
};
const char *const kThemeNames[] = {PT_("Blue and gold"), PT_("Black and red"), PT_("Purple and green"),
                                   PT_("Steel and amber"), PT_("Gold and cyan")};
const int kCustom = sizeof kThemes / sizeof kThemes[0];

// Colours that say what a thing is (ready, a warning): never recoloured, though a custom
// theme's guard may lighten them.
UiColor *const kFixed[] = {&kOk, &kLbWarn};
const int kFixedCount = sizeof kFixed / sizeof kFixed[0];
UiColor g_standardFixed[kFixedCount];

float Lin(float c) { return c <= 0.04045f ? c / 12.92f : powf((c + 0.055f) / 1.055f, 2.4f); }
float Gam(float c)
{
  c = c < 0.0f ? 0.0f : c > 1.0f ? 1.0f : c;
  return c <= 0.0031308f ? c * 12.92f : 1.055f * powf(c, 1.0f / 2.4f) - 0.055f;
}

// OKLCh, the hue in radians.
struct Lch {
  float L, C, h;
};

Lch LchOf(const UiColor &c)
{
  const float r = Lin(c.r), g = Lin(c.g), b = Lin(c.b);
  const float l = cbrtf(0.4122214708f * r + 0.5363325363f * g + 0.0514459929f * b);
  const float m = cbrtf(0.2119034982f * r + 0.6806995451f * g + 0.1073969566f * b);
  const float s = cbrtf(0.0883024619f * r + 0.2817188376f * g + 0.6299787005f * b);
  const float A = 1.9779984951f * l - 2.4285922050f * m + 0.4505937099f * s;
  const float B = 0.0259040371f * l + 0.7827717662f * m - 0.8086757660f * s;
  return {0.2104542553f * l + 0.7936177850f * m - 0.0040720468f * s, sqrtf(A * A + B * B), atan2f(B, A)};
}

// OKLab to linear sRGB, unclamped.
void LinearOf(float L, float a, float b, float out[3])
{
  const float l = L + 0.3963377774f * a + 0.2158037573f * b;
  const float m = L - 0.1055613458f * a - 0.0638541728f * b;
  const float s = L - 0.0894841775f * a - 1.2914855480f * b;
  const float l3 = l * l * l, m3 = m * m * m, s3 = s * s * s;
  out[0] = 4.0767416621f * l3 - 3.3077115913f * m3 + 0.2309699292f * s3;
  out[1] = -1.2684380046f * l3 + 2.6097574011f * m3 - 0.3413193965f * s3;
  out[2] = -0.0041960863f * l3 - 0.7034186147f * m3 + 1.7076147010f * s3;
}

bool InGamut(const float x[3])
{
  for (int i = 0; i < 3; ++i)
    if (x[i] < -0.002f || x[i] > 1.002f)
      return false;
  return true;
}

// Back from L, C, h, taking chroma down until it fits sRGB (themes.js rgbOf).
UiColor RgbOf(float L, float C, float h, float fAlpha)
{
  float x[3];
  LinearOf(L, C * cosf(h), C * sinf(h), x);
  if (!InGamut(x)) {
    float lo = 0.0f, hi = C;
    for (int i = 0; i < 18; ++i) {
      const float mid = (lo + hi) * 0.5f;
      LinearOf(L, mid * cosf(h), mid * sinf(h), x);
      if (InGamut(x))
        lo = mid;
      else
        hi = mid;
    }
    LinearOf(L, lo * cosf(h), lo * sinf(h), x);
  }
  return {Gam(x[0]), Gam(x[1]), Gam(x[2]), fAlpha};
}

// WCAG 2's contrast between two colours.
float Luminance(const UiColor &c)
{
  return 0.2126f * Lin(SDL_clamp(c.r, 0.0f, 1.0f)) + 0.7152f * Lin(SDL_clamp(c.g, 0.0f, 1.0f)) +
         0.0722f * Lin(SDL_clamp(c.b, 0.0f, 1.0f));
}

float Contrast(const UiColor &x, const UiColor &y)
{
  const float a = Luminance(x), b = Luminance(y);
  return (fmaxf(a, b) + 0.05f) / (fminf(a, b) + 0.05f);
}

// A theme's three colours in OKLCh.
struct Refs {
  Lch a, a2, p;
};

Refs RefsOf(const ThemeCustom &t)
{
  return {LchOf(KitRgb(t.accent)), LchOf(KitRgb(t.accent2)), LchOf(KitRgb(t.panel))};
}

enum Family { FAMILY_NONE, FAMILY_ACCENT, FAMILY_ACCENT2 };

Family FamilyOf(const Lch &o)
{
  if (o.C < 0.02f)
    return FAMILY_NONE;
  float h = o.h * 57.29578f;
  h = h < 0.0f ? h + 360.0f : h;
  return h >= 185.0f && h <= 310.0f ? FAMILY_ACCENT : h >= 35.0f && h <= 115.0f ? FAMILY_ACCENT2 : FAMILY_NONE;
}

// A colour's chroma scaled from the standard's reference to the theme's, and its hue the
// theme's. The standard's shades sit off their reference's hue (the Accept button's body is
// an orange brown, 36 degrees under its gold); turned by the same angle, a red pick made
// that body magenta (the user, 2026-10-14), so every shade takes the pick's own hue.
Lch Turned(const Lch &o, const Lch &from, const Lch &to, float fCap)
{
  return {o.L, fminf(fCap, o.C * (from.C > 1e-4f ? to.C / from.C : 0.0f)), to.h};
}

// Lightness through a pick: 0 and 1 stay, the standard's reference lands on the pick.
float RemapL(float L, float from, float to)
{
  to = SDL_clamp(to, 0.02f, 0.98f);
  return L <= from ? L * to / from : to + (L - from) * (1.0f - to) / (1.0f - from);
}

// A colour of the standard theme under theme t, before the guard (themes.js shift): a gold
// turns against the second accent; a blue against the accent, a dark one (under L 0.42)
// blending in OKLab toward the panels, all panel by L 0.24. With the player's lightness each
// family's lightness goes through its pick, the panels' capped at kThemePanelMax.
Lch Shift(const Lch &o, Family f, const ThemeCustom &t, const Refs &r0, const Refs &r)
{
  if (f == FAMILY_ACCENT2) {
    Lch x = Turned(o, r0.a2, r.a2, 0.37f);
    if (t.bLight)
      x.L = RemapL(o.L, r0.a2.L, r.a2.L);
    return x;
  }
  const Lch ac = Turned(o, r0.a, r.a, 0.37f);
  const float pc = fminf(0.09f, o.C * (r0.p.C > 1e-4f ? r.p.C / r0.p.C : 0.0f));
  const float w = SDL_clamp((o.L - 0.24f) / 0.18f, 0.0f, 1.0f);
  const float pa = pc * cosf(r.p.h), pb = pc * sinf(r.p.h);
  const float a = pa + (ac.C * cosf(ac.h) - pa) * w, b = pb + (ac.C * sinf(ac.h) - pb) * w;
  float L = o.L;
  if (t.bLight) {
    const float La = RemapL(o.L, r0.a.L, r.a.L), Lp = RemapL(o.L, r0.p.L, fminf(kThemePanelMax, r.p.L));
    L = Lp + (La - Lp) * w;
  }
  return {L, sqrtf(a * a + b * b), atan2f(b, a)};
}

// The panels a theme draws: the standard's panel colour under it.
UiColor PanelOf(const ThemeCustom &t, const Refs &r0, const Refs &r)
{
  const Lch x = Shift(LchOf(KitRgb(kThemes[0].panel)), FAMILY_ACCENT, t, r0, r);
  return RgbOf(x.L, x.C, x.h, 1.0f);
}

// A colour's lightness raised until it reads on the theme's panels as it did on the
// standard's: 4.5:1 where it had that, else 85% of what it had (a dim label stays dim, and
// visible); false where it needs nothing.
bool Guard(const UiColor &rgb, const UiColor &std, const Lch &x, const UiColor &p0, const UiColor &p, UiColor *pUp)
{
  const float c0 = Contrast(std, p0);
  const float fNeed = c0 >= 4.5f ? 4.5f : c0 >= 1.5f ? fmaxf(1.5f, c0 * 0.85f) : 0.0f;
  if (fNeed == 0.0f || Contrast(rgb, p) >= fNeed - 0.01f)
    return false;
  float lo = x.L, hi = 1.0f;
  UiColor best = RgbOf(1.0f, x.C, x.h, rgb.a);
  for (int i = 0; i < 20; ++i) {
    const float mid = (lo + hi) * 0.5f;
    const UiColor y = RgbOf(mid, x.C, x.h, rgb.a);
    if (Contrast(y, p) >= fNeed) {
      hi = mid;
      best = y;
    } else {
      lo = mid;
    }
  }
  *pUp = best;
  return true;
}

// One colour of the standard under theme t (themes.js solve); *pbLifted when the guard
// raised it. bFixed: a colour that keeps its own, guarded only.
UiColor Solve(const UiColor &c, bool bFixed, const ThemeCustom &t, const Refs &r0, const Refs &r, const UiColor &p0,
              const UiColor &p, bool *pbLifted)
{
  *pbLifted = false;
  const Lch o = LchOf(c);
  const Family f = bFixed ? FAMILY_NONE : FamilyOf(o);
  if (f == FAMILY_NONE && !t.bLight)
    return c;
  const Lch x = f != FAMILY_NONE ? Shift(o, f, t, r0, r) : o;
  const UiColor rgb = f != FAMILY_NONE ? RgbOf(x.L, x.C, x.h, c.a) : c;
  UiColor up;
  if (t.bLight && c.a >= 0.999f && Guard(rgb, c, x, p0, p, &up)) {
    *pbLifted = true;
    return up;
  }
  return rgb;
}

int g_nShown = -1;
ThemeCustom g_shownCustom;

void KeepStandard(void)
{
  if (g_bStandardKept)
    return;
  for (int i = 0; i < kThemedCount; ++i)
    g_standard[i] = *kThemed[i];
  for (int i = 0; i < kFixedCount; ++i)
    g_standardFixed[i] = *kFixed[i];
  g_bStandardKept = true;
}

bool SameCustom(const ThemeCustom &a, const ThemeCustom &b)
{
  return a.accent == b.accent && a.accent2 == b.accent2 && a.panel == b.panel && a.bLight == b.bLight;
}

unsigned int Pack(const UiColor &c)
{
  const auto Byte = [](float v) { return static_cast<unsigned int>(SDL_clamp(v, 0.0f, 1.0f) * 255.0f + 0.5f); };
  return (Byte(c.r) << 16) | (Byte(c.g) << 8) | Byte(c.b);
}

const char *const kCustomKeys[] = {"themeCustomAccent", "themeCustomAccent2", "themeCustomPanel"};

}  // namespace

const float kThemePanelMax = 0.28f;
const ThemeCustom kThemeCustomStart = {0x3fbfa0, 0xff7ab8, 0x04100e, true};

int ThemePresets(void)
{
  return kCustom;
}

ThemeCustom ThemePreset(int n)
{
  return kThemes[n >= 0 && n < kCustom ? n : 0];
}

const char *ThemePresetName(int n)
{
  return PortText(kThemeNames[n >= 0 && n < kCustom ? n : 0]);
}

void ThemeShow(int nTheme, const ThemeCustom &custom)
{
  KeepStandard();
  nTheme = nTheme < 0 || nTheme > kCustom ? 0 : nTheme;
  if (nTheme == g_nShown && (nTheme != kCustom || SameCustom(custom, g_shownCustom)))
    return;
  g_nShown = nTheme;
  g_shownCustom = custom;
  if (nTheme == 0) {
    for (int i = 0; i < kThemedCount; ++i)
      *kThemed[i] = g_standard[i];
    for (int i = 0; i < kFixedCount; ++i)
      *kFixed[i] = g_standardFixed[i];
    return;
  }
  const ThemeCustom t = nTheme < kCustom ? kThemes[nTheme] : custom;
  const Refs r0 = RefsOf(kThemes[0]), r = RefsOf(t);
  const UiColor p0 = KitRgb(kThemes[0].panel), p = PanelOf(t, r0, r);
  bool bLifted;
  for (int i = 0; i < kThemedCount; ++i)
    *kThemed[i] = Solve(g_standard[i], false, t, r0, r, p0, p, &bLifted);
  for (int i = 0; i < kFixedCount; ++i)
    *kFixed[i] = Solve(g_standardFixed[i], true, t, r0, r, p0, p, &bLifted);
}

ThemeReport ThemeReportOf(const ThemeCustom &t)
{
  KeepStandard();
  const Refs r0 = RefsOf(kThemes[0]), r = RefsOf(t);
  const UiColor p0 = KitRgb(kThemes[0].panel), p = PanelOf(t, r0, r);
  ThemeReport out;
  out.nLifted = 0;
  out.fMinText = 0.0f;
  float fMin = 99.0f;
  bool bLifted;
  for (int k = 0; k < kThemedCount + kFixedCount; ++k) {
    const bool bFixed = k >= kThemedCount;
    const UiColor &c = bFixed ? g_standardFixed[k - kThemedCount] : g_standard[k];
    if (c.a < 0.999f)
      continue;
    const UiColor v = Solve(c, bFixed, t, r0, r, p0, p, &bLifted);
    out.nLifted += bLifted ? 1 : 0;
    if (Contrast(c, p0) >= 4.5f)
      fMin = fminf(fMin, Contrast(v, p));
  }
  out.fMinText = fMin < 99.0f ? fMin : 0.0f;
  out.accent = Pack(Solve(KitRgb(kThemes[0].accent), false, t, r0, r, p0, p, &bLifted));
  out.accent2 = Pack(Solve(KitRgb(kThemes[0].accent2), false, t, r0, r, p0, p, &bLifted));
  out.panel = Pack(p);
  return out;
}

ThemeCustom ThemeCustomSaved(void)
{
  ThemeCustom c = kThemeCustomStart;
  unsigned int *apColour[] = {&c.accent, &c.accent2, &c.panel};
  for (int k = 0; k < 3; ++k) {
    char sz[16];
    if (PortSettingString("hyperion", kCustomKeys[k], sz, sizeof sz) && sz[0] == '#' && strlen(sz) == 7)
      *apColour[k] = static_cast<unsigned int>(strtoul(sz + 1, NULL, 16));
  }
  double d = 1.0;
  if (PortSettingNumber("hyperion", "themeCustomLight", &d))
    c.bLight = d != 0.0;
  return c;
}

void ThemeCustomSave(const ThemeCustom &c)
{
  const unsigned int anColour[] = {c.accent, c.accent2, c.panel};
  for (int k = 0; k < 3; ++k) {
    char sz[16];
    SDL_snprintf(sz, sizeof sz, "#%06x", anColour[k] & 0xffffff);
    PortSettingSetString("hyperion", kCustomKeys[k], sz);
  }
  PortSettingSetNumber("hyperion", "themeCustomLight", c.bLight ? 1.0 : 0.0);
}

void ThemeApply(void)
{
  double dTheme = 0.0;
  PortSettingNumber("hyperion", "colourTheme", &dTheme);
  ThemeShow(static_cast<int>(dTheme), ThemeCustomSaved());
}

void ThemeHsv(unsigned int nRgb, float *pfH, float *pfS, float *pfV)
{
  const float r = ((nRgb >> 16) & 0xff) / 255.0f, g = ((nRgb >> 8) & 0xff) / 255.0f, b = (nRgb & 0xff) / 255.0f;
  const float mx = fmaxf(r, fmaxf(g, b)), mn = fminf(r, fminf(g, b)), d = mx - mn;
  float h = 0.0f;
  if (d > 0.0f)
    h = mx == r ? fmodf((g - b) / d, 6.0f) : mx == g ? (b - r) / d + 2.0f : (r - g) / d + 4.0f;
  h = fmodf(h * 60.0f + 360.0f, 360.0f);
  *pfH = h;
  *pfS = mx > 0.0f ? d / mx : 0.0f;
  *pfV = mx;
}

unsigned int ThemeFromHsv(float h, float s, float v)
{
  const auto F = [&](float n) {
    const float k = fmodf(n + h / 60.0f, 6.0f);
    return v - v * s * fmaxf(0.0f, fminf(fminf(k, 4.0f - k), 1.0f));
  };
  const UiColor c = {F(5.0f), F(3.0f), F(1.0f), 1.0f};
  return Pack(c);
}

float ThemeLightOf(unsigned int nRgb)
{
  return LchOf(KitRgb(nRgb)).L;
}

}  // namespace FrontKit
