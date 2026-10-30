// port_frontkit.h -- the front end's kit (port_front.cpp and its screens; docs/front-end.md):
// Clay without its macros, the design's colours, StarCraft II's controls as the Settings
// dialog draws them, and the frame's input. Presentation only.
#ifndef PORT_FRONTKIT_H
#define PORT_FRONTKIT_H

#include <deque>
#include <string>
#include <vector>
#include <SDL3/SDL.h>
#include "clay.h"
#include "port_text.h"
#include "port_ui.h"

struct _FILETIME;  // windows.h's FILETIME

namespace FrontKit {

// UiRgb, for constants: each file has its own, made as it compiles.
constexpr UiColor KitRgb(unsigned int nRgb, float fAlpha = 1.0f)
{
  return UiColor{((nRgb >> 16) & 0xFF) / 255.0f, ((nRgb >> 8) & 0xFF) / 255.0f, (nRgb & 0xFF) / 255.0f, fAlpha};
}

// The dot between two facts on a line, with its spaces, in UTF-8.
const char *const kDot = " \xc2\xb7 ";
// the months' short names, in English: PortTextC("month", kMonths[i]) translates one
const char *const kMonths[] = {PTC_("month", "Jan"), PTC_("month", "Feb"), PTC_("month", "Mar"), PTC_("month", "Apr"),
                               PTC_("month", "May"), PTC_("month", "Jun"), PTC_("month", "Jul"), PTC_("month", "Aug"),
                               PTC_("month", "Sep"), PTC_("month", "Oct"), PTC_("month", "Nov"), PTC_("month", "Dec")};

// The design's colours (front.css, :root and .fe-d), in the standard theme; Settings'
// theme writes them all again (ThemeApply), so they are variables.

extern UiColor kInk;
extern UiColor kInkDim;
extern UiColor kInkFaint;
extern UiColor kGold;
extern UiColor kOk;
extern UiColor kTool;
extern UiColor kPanelSolid;
extern UiColor kRule;
extern UiColor kGlass;
extern UiColor kGlassHi;
extern UiColor kGoldWash;
extern UiColor kGoldLine;
extern UiColor kNone;
static const float kBlur = 8.0f;  // --d-blur, in units

// SC2's colours (front.css, the --sc- names).
extern UiColor kScInkHi;
extern UiColor kScGlow;
extern UiColor kScInkButton;
extern UiColor kScInkHead;
extern UiColor kScInkLabel;
extern UiColor kScInkMsg;
extern UiColor kScInkMenu;
extern UiColor kScInkTip;
extern UiColor kScInkMini;
extern UiColor kScInkOff;
extern UiColor kScInkAccept;
extern UiColor kScBlackout;
extern UiColor kScFill;
// a modal dialog's fill: kScFill without the planet showing through
extern UiColor kScFillSolid;
extern UiColor kScRim;
extern UiColor kScRimGlow;
extern UiColor kScBevel;
extern UiColor kScNotch;
extern UiColor kScWell;
extern UiColor kScWellRim;
extern UiColor kScBlack;
extern UiColor kScShade;
extern UiColor kScCheckTop;
extern UiColor kScCheckBot;
extern UiColor kScCheckRim;
extern UiColor kScCheckHi;
extern UiColor kScCheckRimHi;
extern UiColor kScDdRim;
extern UiColor kScMenuRim;
extern UiColor kScMenuFill;
extern UiColor kScMenuSel;
extern UiColor kScTrack;
extern UiColor kScTrackGlow;
extern UiColor kScSliderFill;
extern UiColor kScKnob;
extern UiColor kScKnobRim;
extern UiColor kScKnobHi;
extern UiColor kScKnobRimHi;
extern UiColor kScTipFill;
extern UiColor kScTipRim;
extern UiColor kScCountFill;

// A raised control's look: its fill from top to foot, its rim, the lit lip inside its foot
// and the halo outside.

struct ScLook {
  UiColor top, bot, rim, lip, halo;
  float fRadius;  // SC2 units
};

extern ScLook kScButton;
extern ScLook kScButtonHi;
extern ScLook kScAccept;
extern ScLook kScAcceptHi;
extern ScLook kScTab;
extern ScLook kScTabHi;
extern ScLook kScTabOn;
extern ScLook kScMini;
extern ScLook kScMiniHi;
extern ScLook kScDrop;
extern ScLook kScDropHi;
extern ScLook kScOff;

// Direction D's home screen (front.css .fe-d: the nav bar, avatar, card, taskbar, toasts).
// the nav labels, the home mark, links
extern UiColor kScInkNav;
// a tooltip head, a toast title
extern UiColor kScInkTipHead;
// the taskbar clock
extern UiColor kScInkClock;
// the taskbar icons, a close button
extern UiColor kScIcon;
extern UiColor kScIconHi;
// the nav bar: black under its blur
extern UiColor kScBarUnder;
// its gradient, top to foot, and its lit foot
extern UiColor kScBarTop;
extern UiColor kScBarBot;
extern UiColor kScBarEdge;
// its highlight sweeps
extern UiColor kScSweep;
// its dividers
extern UiColor kScDividerTop;
extern UiColor kScDividerBot;
// a section under the mouse
extern UiColor kScHoverLine;
extern UiColor kScHoverGlow;
// the current section: its glow and underline
extern UiColor kScSelGlow;
extern UiColor kScSelLine;
extern UiColor kScSelSoft;
extern UiColor kScSelLine2;
// the avatar's rings, glow, glass and face
extern UiColor kScAvaOuter;
extern UiColor kScAvaMid;
extern UiColor kScAvaInner;
extern UiColor kScAvaGlow;
extern UiColor kScAvaGlass;
extern UiColor kScAvaHover;
extern UiColor kScAvaHoverBand;
extern UiColor kScAvaFaceTop;
extern UiColor kScAvaFaceBot;
// the player card tab
extern UiColor kScPartyTop;
extern UiColor kScPartyFillTop;
extern UiColor kScPartyFillBot;
extern UiColor kScPartyBot;
// the bell's count
extern UiColor kScCountRim;
extern UiColor kScCountBevel;
// the clock's plate
extern UiColor kScClockFill;
extern UiColor kScClockRim;
// a notification: fill, rim, halo, shimmer
extern UiColor kScToastTop;
extern UiColor kScToastBot;
extern UiColor kScToastRim;
extern UiColor kScToastEdge;
extern UiColor kScToastShimmer;
// a raised panel's shadow
extern UiColor kDShadow;
// a taskbar button, and under the mouse
extern ScLook kScTask;
extern ScLook kScTaskHi;

// The skirmish set-up (lobby.css): the inks of a map's facts and their descriptions, an
// empty slot's, the boxes' fill and rim, a slot's fill, under the mouse and its rim, the
// dividers, the rim of what plays by Hyperion's rules, and the mark of colours too close
// (which keeps its own colour in every theme).
extern UiColor kLbInkInfo;
extern UiColor kLbInkDesc;
extern UiColor kLbInkEmpty;
extern UiColor kLbInner;
extern UiColor kLbInnerRim;
extern UiColor kLbItem;
extern UiColor kLbItemHi;
extern UiColor kLbItemRim;
extern UiColor kLbDivider;
extern UiColor kLbHyperionRim;
extern UiColor kLbWarn;

// Settings' colour theme (Interface, "colourTheme": 0 blue and gold, the standard, 1 black
// and red, 2 purple and green, 3 steel and amber, 4 gold and cyan, 5 custom): the three
// colours, accent, second accent and panels, each 0xRRGGBB, and for a custom one whether its
// lightness is the player's ("themeCustomAccent", "themeCustomAccent2", "themeCustomPanel" as
// "#rrggbb", "themeCustomLight"). ThemeApply sets the colours above from the saved settings,
// ThemeShow from a theme not yet saved; the solver is port_frontkit.cpp's Solve (themes.js).
struct ThemeCustom {
  unsigned int accent, accent2, panel;
  bool bLight;  // lightness the player's, not the theme's
};
extern const ThemeCustom kThemeCustomStart;
extern const float kThemePanelMax;
void ThemeApply(void);
void ThemeShow(int nTheme, const ThemeCustom &custom);
int ThemePresets(void);
ThemeCustom ThemePreset(int n);
const char *ThemePresetName(int n);
ThemeCustom ThemeCustomSaved(void);
void ThemeCustomSave(const ThemeCustom &c);
// What a custom theme draws, for the picker: the accent, second accent and panels as they
// land on screen, how many colours the guard lifted, and the weakest text on the panels
// (WCAG contrast).
struct ThemeReport {
  unsigned int accent, accent2, panel;
  int nLifted;
  float fMinText;
};
ThemeReport ThemeReportOf(const ThemeCustom &t);
// The picker's colour space: hue 0 to 360, saturation and value 0 to 1; and a colour's OKLab
// lightness.
void ThemeHsv(unsigned int nRgb, float *pfH, float *pfS, float *pfV);
unsigned int ThemeFromHsv(float h, float s, float v);
float ThemeLightOf(unsigned int nRgb);

// The frame's input, as the screen's loop gathers it.
extern float g_mouseX;
extern float g_mouseY;
extern bool g_bDown;
extern bool g_bPressed;          // this frame
extern bool g_bReleased;
extern uint32_t g_nPressedOn;    // the element the button went down on
extern std::vector<int> g_keys;  // the frame's key presses (virtual keys)
extern std::string g_typed;      // the frame's typed characters (WM_CHAR), in UTF-8
extern int g_nWheel;             // the frame's wheel turns, 120 a notch, up positive
// The frame's text and drawing, alive (and unmoved) until its layout is drawn.
extern std::deque<std::string> g_strings;

struct Decl {
  Clay_ElementDeclaration d;
  Decl() { SDL_zero(d); }
};

// A control's draw and its look, alive until the frame's layout is drawn.
struct ScDraw {
  ScLook look;
  float f;
  int n;
};
extern std::deque<ScDraw> g_draws;
extern std::deque<UiCustom> g_customs;

Clay_String Str(const char *psz, int nLength = -1);
const char *Keep(const std::string &s);
Clay_ElementId Id(const char *psz, int nIndex = 0);
Clay_SizingAxis Fixed(float f);
Clay_SizingAxis Grow(void);
Clay_SizingAxis Fit(float fMax = 0.0f);
Clay_Color Col(UiColor c);
Clay_Padding Pad(float l, float r, float t, float b);
uint16_t Gap(float f);
Clay_BorderElementConfig Border(UiColor c, float l, float r, float t, float b);
void Open(const Decl &x);
void Close(void);
// Each frame's Opens and Closes are counted: LayoutCheckBegin before the screens declare
// their elements, LayoutCheckEnd after, which logs what was left open.
void LayoutCheckBegin(void);
void LayoutCheckEnd(const char *pszWhere);
void Text(const char *psz, UiFace face, float fSize, float fSpacingEm, UiColor c, int nUnderline = -1,
          float fLineHeight = 0.0f, bool bCenter = false);
// Text with the design's glow round it (UI_TEXT_GLOW), in kScGlow.
void TextGlow(const char *psz, UiFace face, float fSize, float fSpacingEm, UiColor c, int nUnderline = -1,
              float fLineHeight = 0.0f, bool bCenter = false);
std::string Upper(const char *psz);
// A file's time in local time, as the replays and Load Game show it: "18 Oct 2026, 14:05".
std::string FileDate(const _FILETIME &ft);
// A number with its thousands marked, "12,345".
std::string Thousands(long long n);
// s as it fits fWidth pixels in face at fSize units, cut short with an ellipsis when it does
// not. Lists cut their cells so rather than clip them: Clay keeps scroll records for only ten
// clipping elements a frame, which the panels that scroll need.
std::string Elide(const std::string &s, UiFace face, float fSize, float fWidth);
// 1.17's game types by the game header's number ("Melee", "Top vs Bottom"), "Custom" for others.
const char *GameTypeName(int n);
bool Over(Clay_ElementId id);
bool Clicked(Clay_ElementId id);
// Whether a click on id, where Clicked(id) is true, is a double click: the same element
// clicked again within 450 ms.
bool DoubleClicked(Clay_ElementId id);
bool Key(int nVk);
UiCustom *Custom(void (*pfnDraw)(float, float, float, float, void *), const ScLook &look, float f = 0.0f, int n = 0);
float Sc(float f);
// SC2's units as Pad, Gap, Border and Text take them: 0.9 of a stage pixel (front.css --u),
// which Sc then turns into pixels.
float Fs(float f);
// A gap that takes the row's room.
void Spacer(void);
// A box to lay out, named pszId and nIndex (NULL: no name).
Decl Box(const char *pszId, int nIndex = 0);
// A dialog's title, centred (front.css .sc-title).
void DialogTitle(const char *psz);
// The frame's wheel as rows toward a list's end, nPerNotch a notch (up, toward its start, is
// negative), when bOver: the mouse is over the list. A touchpad's part notches add up.
int WheelRows(bool bOver, int nPerNotch = 3);
// The same in pixels, fPerNotch a notch.
float WheelPixels(bool bOver, float fPerNotch);
std::vector<std::string> Words(const char *psz);
int KeyAt(const char *psz, char cKey);
void DrawScFrame(float x, float y, float w, float h, void *);
void DrawScRaised(float x, float y, float w, float h, void *pCtx);
void DrawScCheck(float x, float y, float w, float h, void *pCtx);
void DrawScSlider(float x, float y, float w, float h, void *pCtx);
void DrawScQuery(float x, float y, float w, float h, void *pCtx);
void DrawScBox(float x, float y, float w, float h, void *pCtx);
Decl Floating(const char *pszId, int nIndex, float x, float y, int nZ, Clay_FloatingAttachPointType element,
              Clay_FloatingAttachPointType parent);
// The size a one-line label takes in its button, down to 60% of its own, as a translation may
// be longer than the English the button was sized for: the button's fixed width, else its width
// at the last frame, less its padding (fPad each side) and fBeside pixels for what sits beside
// the label.
float FitSize(const Decl &b, const char *psz, UiFace face, float fSize, float fSpacingEm, float fPad,
              float fBeside = 0.0f);
bool ScButton(Decl b, const char *pszLabel, char cKey, bool bGold, bool bEnabled = true);
// A check box and its label in the row b, 40 units high, its key underlined: true when clicked.
bool ScCheck(Decl b, const char *pszLabel, char cKey, bool bOn, bool bEnabled = true);
bool ScMini(Decl b, const char *pszLabel, char cKey, bool bEnabled = true);
// A tab of a row of them (bench.css .bm-tabs): ScMini at least 170 units wide and 42 high, rimmed
// in Hyperion's gold while it is the one up.
bool ScMiniTab(Decl b, const char *pszLabel, char cKey, bool bOn);
// SC2's edit box (StandardEditBoxTemplate): the text, with the frame's typing and Backspace
// applied while it has the keyboard, at most nMax characters; the placeholder while empty.
void ScEdit(Decl b, std::string *pText, size_t nMax, const char *pszPlaceholder, bool bFocus = true);

}  // namespace FrontKit

#endif
