// port_ui.h -- the front end's drawing layer: primitives, text and layout (port_ui.cpp,
// docs/front-end.md, F1). Presentation only.
#ifndef PORT_UI_H
#define PORT_UI_H

#include <stddef.h>
#include <string>
#include <vector>

// One element drawn on the GPU, as shaders/ui.hlsl's UiPrim has it.
struct UiPrim {
  float rect[4];    // x, y, width, height, in the target's pixels
  float color[4];   // straight RGBA, 0 to 1
  float color2[4];  // the gradient's far colour
  float uv[4];      // a glyph's place in the atlas, 0 to 1
  float clip[4];    // x0, y0, x1, y1
  float shape[4];   // corner radius, border width, kind (UiPrimKind), gradient (0, 1 down, 2 across)
};

enum UiPrimKind { UI_FILL, UI_RING, UI_GLYPH, UI_PLANET, UI_BLUR, UI_PICTURE };

// The design's faces (--fe-font, --fe-font-wide, --bm-mono): Saira for text, Michroma for
// titles and labels, Saira SemiBold where the design sets text at weight 600, and IBM Plex Mono
// Medium for numbers that count, whose figures keep their width.
// Chakra Petch's four cuts are the HUD tooltips': 400, 500, 600 and 700.
enum UiFace {
  UI_FACE_TEXT, UI_FACE_WIDE, UI_FACE_BOLD, UI_FACE_MONO, UI_FACE_PETCH, UI_FACE_PETCH_MEDIUM, UI_FACE_PETCH_SEMI,
  UI_FACE_PETCH_BOLD, UI_FACES
};

struct UiColor {
  float r, g, b, a;
};

// 0xRRGGBB and an alpha.
UiColor UiRgb(unsigned int nRgb, float fAlpha = 1.0f);

// A frame's list. The design is drawn on a stage 1080 units high, and at least 1920 wide:
// a window narrower than 16:9 is that wide and taller. UiScale is the target's pixels per
// unit, and UiPx turns units into pixels.
void UiBegin(int nWidth, int nHeight);
float UiScale(void);
float UiPx(float fUnits);
int UiWidth(void);
int UiHeight(void);
void UiFill(float x, float y, float w, float h, UiColor c, float fRadius = 0.0f);
void UiGradient(float x, float y, float w, float h, UiColor c, UiColor c2, bool bAcross, float fRadius = 0.0f);
// Across a rect from c2 at its sides to c at its middle, in one piece (two gradients meeting at the
// middle leave a column there that neither covers whole).
void UiGradientMid(float x, float y, float w, float h, UiColor c, UiColor c2);
void UiRing(float x, float y, float w, float h, UiColor c, float fRadius, float fWidth);
void UiPlanet(float x, float y, float w, float h);
// The planet behind a see-through panel, blurred over fBlur pixels.
void UiBlur(float x, float y, float w, float h, float fRadius, float fBlur);
// A line, at any angle; it keeps the turn UiSetTurn set for what follows.
void UiLine(float x0, float y0, float x1, float y1, UiColor c, float fWidth);
// Rectangles and rings drawn after this turn by fAngle radians about (x, y); 0 stops it.
void UiSetTurn(float fAngle, float x, float y);
void UiPushClip(float x0, float y0, float x1, float y1);
void UiPopClip(void);

// Text, at a size in pixels; fSpacing is CSS's letter-spacing, in pixels. UiText draws from
// a baseline and returns the pen's advance; nUnderline underlines that character (a hotkey's
// letter), -1 none.
float UiText(float x, float fBaseline, UiFace face, float fSize, float fSpacing, UiColor c, const char *pszUtf8,
             int nBytes = -1, int nUnderline = -1);
// The glow round text drawn with one (Clay text whose userData has UI_TEXT_GLOW): the
// design's text-shadow of its glow colour, as the theme sets it.
enum { UI_TEXT_GLOW = 0x10000 };
void UiSetGlow(UiColor c);
float UiTextWidth(UiFace face, float fSize, float fSpacing, const char *pszUtf8, int nBytes = -1);
// Whether the face has a letter for every character of the text but the controls.
bool UiTextCovered(UiFace face, const char *pszUtf8, size_t nBytes);
// The text in capitals, as the design sets its headings: Latin, Greek and Cyrillic by Unicode's
// case, a sharp s as SS; the scripts without capitals as they are.
std::string UiUpper(const char *pszUtf8);
// The face's ascent, descent (negative) and line gap at a size, in pixels.
void UiFontMetrics(UiFace face, float fSize, float *pfAscent, float *pfDescent, float *pfLineGap);
// Which font each face is: Hyperion's own, a file HYPERION_FONT names, or the system's fallback.
const char *UiFaceSource(UiFace face);

// Pictures: RGBA images the screens make (a map's minimap), each in a slot of the picture
// atlas, UI_PICTURE_SIDE pixels square at most, four slots a row; a wider picture takes the
// slots to its right, which its screen keeps free. UiPicture draws a slot over a rectangle,
// its colours times c, filtered or, with bNearest, a pixel a square of the screen's. Slots 0
// to 15 are the screens' (in a game, 14 and 15 a benchmark run's minimap and wireframes); 16 to 21 the countdown's (port_intro.cpp); 22 and 23 the colour
// picker's (port_front_theme.cpp); 24 and 25 the patch notes', one picture up to 768 wide
// (port_front_notes.cpp); 26 and 27 the AI lab's wall, nine maps a slot (port_labwall.cpp); 28 to
// 31 the LAN console's Wall, six a slot (port_front_lanhost.cpp); 32 the nav bar's home mark and
// 33 its glows (port_front_home.cpp); 34 a HUD design's sheet of the console's parts
// (port_gpu_hud.cpp) and 35 the cursor over it (port_hwcursor.cpp); 36 to 39 a HUD design's
// selection grid, one picture four slots wide (port_gpu_hud.cpp); 40 the players' stats' sheet
// of unit icons (port_hudstats.cpp), and 41 to 43 free.
enum { UI_PICTURE_SIDE = 384, UI_PICTURE_SLOTS = 44, UI_PICTURE_ACROSS = 4 };
void UiPictureSet(int nSlot, const unsigned char *pRgba, int nWidth, int nHeight);
void UiPicture(float x, float y, float w, float h, int nSlot, UiColor c, float fRadius = 0.0f, bool bNearest = false);
// Part of a slot's picture, its pixels from (u0, v0) to (u1, v1), over a rectangle: a sheet of
// small pictures in one slot (the benchmark's wireframes).
void UiPicturePart(float x, float y, float w, float h, int nSlot, float u0, float v0, float u1, float v1, UiColor c,
                   bool bNearest = false);

const UiPrim *UiPrims(int *pnCount);
// A character's coverage at a size in pixels, a byte a pixel, *pnWidth by *pnHeight: its ink
// only, for a picture made of it (the countdown's digits). False when the face cannot draw it.
bool UiGlyphCover(UiFace face, float fSize, unsigned int c, std::vector<unsigned char> *pCover, int *pnWidth,
                  int *pnHeight);
// The glyph atlas, a byte a pixel, and the rows changed since UiAtlasClean.
const unsigned char *UiAtlas(int *pnWidth, int *pnHeight);
bool UiAtlasDirty(int *pnFirstRow, int *pnRows);
void UiAtlasClean(void);
// The picture atlas, four bytes a pixel, and the rows changed since UiPicturesClean.
const unsigned char *UiPictures(int *pnWidth, int *pnHeight);
bool UiPicturesDirty(int *pnFirstRow, int *pnRows);
void UiPicturesClean(void);

// A custom element in a layout (Clay's CUSTOM): drawn in its place in the order, over the
// planet blurred behind it when fBlur is above 0, then its background, then pfnDraw.
struct UiCustom {
  float fBlur;
  void (*pfnDraw)(float x, float y, float w, float h, void *pCtx);
  void *pCtx;
};

// Layout (Clay). UiLayoutBegin starts a frame's layout at the target's size with the pointer
// where it is; UiLayoutEnd lays it out and draws what it holds into the frame's list.
struct Clay_RenderCommandArray;
void UiLayoutBegin(float fPointerX, float fPointerY, bool bPointerDown);
void UiLayoutEnd(void);

// Motion (the front end's entrances, front.css's "d-at" timeline): an element of this frame's
// layout, by its Clay id, drawn with its opacity times fAlpha, moved by fDx, fDy pixels and
// scaled by fScale about its centre, with all it holds. A draw takes the smallest element
// with a motion whose box holds its centre. Set between UiLayoutBegin and UiLayoutEnd.
struct UiMotion {
  float fAlpha;
  float fDx, fDy;
  float fScale;
};
void UiMotionSet(unsigned int nClayId, const UiMotion &m);

// A CSS cubic-bezier(x1, y1, x2, y2) at t (0 to 1), and the design's three: --sc-ease
// (0.15, 0.6, 0.4, 1), --sc-ease-even (0.3, 0.55, 0.7, 0.45); --sc-ease-auto is linear.
float UiBezier(float t, float x1, float y1, float x2, float y2);
float UiEase(float t);
float UiEaseEven(float t);
// t's place from fFrom to fFrom + fLength, 0 to 1.
float UiSpan(float t, float fFrom, float fLength);

// port_casc.cpp: a file out of the Remastered install, SDL_malloc'd, or NULL. PortCascFolder
// is the install found (empty with none); PortCascCheck finds the install at or above a
// folder the player picked; PortCascUse opens one Settings was given (port_casc.cpp).
int PortCascAvailable(void);
void *PortCascRead(const char *pszName, size_t *pnSize);
// Remastered's paid content (port_casc.cpp, the PRD's decision of 2026-10-21): the HD art and
// the recordings that StarCraft: Remastered's purchase unlocks in Blizzard's own game, which
// a free install downloads all the same. PortCascRead refuses them unless this is true, and
// while the user holds HD it never is.
bool PortRemasteredOwned(void);
bool PortCascPaid(const char *pszName);
const char *PortCascFolder(void);
// The open install's text language as a locale ("enUS", "plPL"; English with none open), a
// folder's, and a locale's name in English ("Polish") (port_casc.cpp).
const char *PortCascTextLocale(void);
const char *PortCascSpeechLocale(void);  // its speech language, the same way
void PortCascTextLocaleOf(const char *pszFolder, char *pszOut, size_t nOut);
const char *PortCascLanguageName(const char *pszLocale);
// Whether a test's install in another language (HYPERION_TEST_TEXT_LOCALE) lacks the file.
bool PortCascHidden(const char *pszName);
int PortCascCheck(const char *pszPicked, char *pszOut, size_t nOut);
int PortCascUse(const char *pszFolder);

#endif
