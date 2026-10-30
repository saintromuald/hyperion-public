// port_front.cpp -- Hyperion's own front end: the screen loop, the home screen, the nav's
// dispatch, the keys, the entrance and leaving motions, the front end's log. Presentation
// only (docs/modules/front-end.md, which has the screens and the HYPERION_FRONT_* checks).
//
// The home screen is the "Orbit hub" design: the sizes and colours are its front.css,
// on its stage 1080 units high. It takes do_main_dlg's place (gluMain.cpp) with a loop of its own,
// pumping the window's messages, which WndProc hands here while it is up, and drawing through
// the GPU presenter, then gives back the choice 1.17's main menu would have, so everything
// after it is the game's own. The other screens run in this loop in their 1.17 screens'
// place; port_front_internal.h is what the files share. Without the GPU presenter, or with
// "Hyperion's menus" off (HYPERION_FRONT overrides it), 1.17's menu runs.
#include <windows.h>
#include <string.h>
#if defined(__EMSCRIPTEN__)
#include <emscripten.h>  // EM_ASM: a seat in the browser tells its page how it stands
#endif
#include <string>
#include <SDL3/SDL.h>
#include "../platform/port_log.h"  // PortLogger
#include "port_frontkit.h"
#include "port_frontscreens.h"
#include "../lab/port_labfarm.h"  // PortLabFarmPump
#include "../platform/port_discord.h"  // PortDiscordPump
#include "../lab/port_labwatch.h"  // PortLabWatchLaunch, PortLabWatchAfterGame
#include "../tools/port_bench.h"  // PortBenchBackTo, PortBenchSettings, PortBenchFromMenus
#include "../tools/port_crash.h"  // PortCrashNote
#include "port_front.h"
#include "../gameplay/port_launch.h"  // PortLaunchStart, PortLaunchMapFile, PortLaunchCanvas
#include "../platform/port_paths.h"  // PortCommandOptions
#include "../render/port_present.h"
#include "../platform/port_winmsg.h"
#include "gluCmpgn.h"        // ReadCharacterFile
#include "net_glue.h"        // SetLocalPlayerName
#include "glues.h"           // glues_reset_prev_pal
#include "../tools/port_control.h"
#include "../render/port_gpu.h"
#include "../lan/port_front_lan.h"   // Lan::ServerStart, Lan::ToConsole
#include "../lan/port_lanfeed.h"     // a LAN match watched
#include "../data/port_mods.h"
#include "../platform/port_settings.h"
#include "../gameplay/port_trainer.h"     // TrainerSetup, PortTrainerFromMenus
#include "../gameplay/port_scoregame.h"   // ScoreRead, ScoreStandIn
#include "port_ui.h"
#include "port_world.h"
#include "../../gen/globals117.h"
#include "port_front_internal.h"
#include "../tools/port_startup.h"  // PortStartupStep, PortStartupFrame

using namespace FrontKit;

namespace Front {

// ---------------------------------------------------------------------------
// State.

bool g_bChecked;

bool g_bOn;

bool g_bUp;               // the loop is running: WndProc's messages come here

int g_nChoice;            // what the screen chose: 1.17's main-menu result, 0 while it runs

int g_nMod;               // PortMods, the active one

bool g_bMods;             // the Mods screen is open

int g_nModPick;           // the mod its page shows

std::string g_frontCanvas;  // the canvas's tileset when the live channel asked for it

std::string g_frontMap;     // or a map file it asked for (map_load), Use Map Settings when

bool g_bFrontMapUms;        // g_bFrontMapUms

bool g_bMenuOpen;

bool g_bNewsOpen = true;

const char *g_pszNext;    // after the profile screen: "skirmish", "replays", or NULL

bool g_bPassProfile;      // the profile screen is passed when the profile is known

int g_nScreen;

float g_fFrameDt;         // the frame's seconds, for what eases

bool g_bSkirmishGame;     // the game being played was set up on the skirmish screen

bool g_bReplayGame;       // the replays screen started the replay being played

bool g_bExitNext;         // a screen past home was told to exit: home exits at once

// A section or a Menu entry chosen on a screen with a loop of its own (skirmish, replays, the
// campaign) that home starts: the screen ends as its Home would end it, and home chooses this
// before it draws (PortFrontMainMenu), so the bar goes from one section to another.
const char *g_pszSwitch;

bool g_bFromScore;        // the game's score screen was OK'd: its screen comes back as it is

// The entrance (front.css's "d-at" timeline): the seconds since the home screen came
// up, below 0 once it has all come in. The first time, the whole of it; later, the notes.
float g_fEnter = -1.0f;

float g_fReturn = -1.0f;  // the return from a game into its screen (Return), or -1

// A section come in from another (SwitchIn): the seconds since, or -1; and the screen the last
// frame drew, across the screens' loops, -1 before the first.
float g_fSwitch = -1.0f;

int g_nScreenShown = -1;

// A section the bar chose, held while the screen it leaves fades out (NavPick): the seconds
// since, or -1, and the choice, made once the fade is over.
float g_fOut = -1.0f;

const char *g_pszOut;

Uint64 g_qwHandedBack;  // when FrontHandBack last kept the frame, for RunScreen's log line, or 0

bool g_bBackFromGame;     // AfterGame sent the game back to the screen it came from

bool g_bEnterAll;

bool g_bEntered;

// Leaving for a game (front.css 1386-1391): the seconds since the choice, below 0 while
// none is made, and the choice, held until the screen has gone to black.
float g_fLeave = -1.0f;

int g_nLeaving;

const PortMod &ActiveMod(void)
{
  return PortMods()[g_nMod];
}

// ---------------------------------------------------------------------------
// Choosing.

const char *ScreenId(void)
{
  return g_nScreen == SCREEN_SKIRMISH ? "skirmish" : g_nScreen == SCREEN_CAMPAIGN ? "campaign"
         : g_nScreen == SCREEN_REPLAYS ? "replays" : g_nScreen == SCREEN_BENCH ? "bench"
         : g_nScreen == SCREEN_TRAINER ? "trainer" : g_nScreen == SCREEN_LAN ? "multiplayer"
         : g_nScreen == SCREEN_LAB ? "lab" : g_nScreen == SCREEN_SCORE ? "score" : "menu";
}

bool Enabled(const char *pszId)
{
  // the score screen ends as 1.17's did: home, which is its OK, or Exit Game
  if (g_nScreen == SCREEN_SCORE)
    return strcmp(pszId, "menu") == 0 || strcmp(pszId, "exit") == 0;
  static const char *const kNotYet[] = {"collections", "hotkeys"};
  for (const char *p : kNotYet)
    if (strcmp(p, pszId) == 0)
      return false;
  // every other screen has the whole bar and Menu (the user, 2026-10-26): Choose goes home first
  return true;
}

// The avatar, the card and Y open the player card (port_front_profile.cpp), on the home
// screen, the campaign's and the replays'; the skirmish keeps its own.
void CardToggle(void)
{
  if (g_nScreen == SCREEN_SKIRMISH)
    return;
  g_bMenuOpen = false;
  ProfileToggle();
}

// What choosing an entry does: the result 1.17's main menu gives for it, or a screen of
// Hyperion's own.
// The score screen opened from home by a check; after a game it ends the loop.
bool g_bScoreFromHome;

void Choose(const char *pszId)
{
  // the score screen: home is its OK, and Exit Game exits after it
  if (g_nScreen == SCREEN_SCORE) {
    if (strcmp(pszId, "exit") == 0 && g_bScoreFromHome) {
      g_nChoice = 2;
    } else if (strcmp(pszId, "menu") == 0 || strcmp(pszId, "exit") == 0) {
      g_bExitNext = strcmp(pszId, "exit") == 0;
      if (g_bScoreFromHome) {
        ScoreClose();
        g_nScreen = SCREEN_HOME;
        WorldSetView("home", false);
      } else {
        g_nChoice = FRONT_SCORE_DONE;
      }
    }
    return;
  }
  if (!Enabled(pszId))
    return;
  g_bMenuOpen = false;
  // Settings and Patch Notes open over whichever screen is up
  if (strcmp(pszId, "settings") == 0) {
    SettingsOpen();
    return;
  }
  if (strcmp(pszId, "notes") == 0) {
    NotesOpen(0);
    return;
  }
  // the section that is up, chosen again
  if (g_nScreen != SCREEN_HOME && strcmp(pszId, ScreenId()) == 0)
    return;
  // the skirmish, replays and campaign screens run in loops of their own, inside 1.17's screens:
  // they end as home, and home goes on to the choice (g_pszSwitch)
  if (g_nScreen == SCREEN_SKIRMISH || g_nScreen == SCREEN_REPLAYS || g_nScreen == SCREEN_CAMPAIGN) {
    if (strcmp(pszId, "exit") == 0)
      g_bExitNext = true;
    else if (strcmp(pszId, "menu") != 0)
      g_pszSwitch = pszId;
    g_nChoice = g_nScreen == SCREEN_SKIRMISH ? FRONT_SKIRMISH_HOME
                : g_nScreen == SCREEN_REPLAYS ? FRONT_REPLAYS_HOME : FRONT_CAMPAIGN_DONE;
    return;
  }
  // the benchmark's set-up, the trainer's entry, Multiplayer and the lab are inside the home
  // screen's loop: home is a step back, and the choice is then home's
  if (g_nScreen == SCREEN_BENCH || g_nScreen == SCREEN_TRAINER || g_nScreen == SCREEN_LAN || g_nScreen == SCREEN_LAB) {
    if (strcmp(pszId, "exit") == 0) {
      g_nChoice = 2;
      return;
    }
    g_nScreen = SCREEN_HOME;
    WorldSetView("home", false);
    if (strcmp(pszId, "menu") == 0)
      return;
  }
  // what starts a game wants a profile first (front.js dGate)
  if ((strcmp(pszId, "campaign") == 0 || strcmp(pszId, "skirmish") == 0 || strcmp(pszId, "replays") == 0) &&
      !ProfileGate(pszId))
    return;
  if (strcmp(pszId, "campaign") == 0) {
    g_pszNext = NULL;
    g_bPassProfile = true;
    g_nChoice = 3;
  } else if (strcmp(pszId, "skirmish") == 0 || strcmp(pszId, "replays") == 0) {
    g_pszNext = pszId[0] == 's' ? "skirmish" : "replays";
    g_bPassProfile = true;
    g_nChoice = 3;
  } else if (strcmp(pszId, "multiplayer") == 0) {
    // Multiplayer, this network's rooms; 1.17's own screens are a button there
    g_nScreen = SCREEN_LAN;
    WorldSetView("campaign", false);
    LanOpen();
  } else if (strcmp(pszId, "intro") == 0) {
    g_nChoice = 8;
  } else if (strcmp(pszId, "credits") == 0) {
    g_nChoice = 9;
  } else if (strcmp(pszId, "exit") == 0) {
    g_nChoice = 2;
  } else if (strcmp(pszId, "mods") == 0) {
    ModsOpen();
  }
}

// Menu background quality: 0 Auto, 1 High, 2 Low ("menuQuality"); -1 until read.
int g_nMenuQuality = -1;

// Auto's share of the planet's lines, lowered while frames come slower than the display's
// refresh and kept for the run.
float g_fPlanetAuto = 1.0f;

double g_dPlanetSlowS = 0.0;

unsigned int g_nPlanetFrames = 0;

unsigned int g_nPlanetSettle = 0;

int MenuQuality(void)
{
  if (g_nMenuQuality < 0) {
    double d = 0.0;
    PortSettingNumber("hyperion", "menuQuality", &d);
    g_nMenuQuality = d >= 0.0 && d <= 2.0 ? static_cast<int>(d) : 0;
  }
  return g_nMenuQuality;
}

// The planet's lines as a share of the frame's: no more than 1440 (High), 720 (Low), or
// Auto's share of High's.
float PlanetScale(int nHeight)
{
  const int nQuality = MenuQuality();
  const float fLines = nQuality == 2 ? 720.0f : 1440.0f;
  const float fHigh = nHeight > fLines ? fLines / nHeight : 1.0f;
  return nQuality == 0 ? fHigh * g_fPlanetAuto : fHigh;
}

// Auto: over each 30 frames, when they came more than a fifth slower than the refresh, the
// planet loses a fifth of its lines, down to 30%; the frames after a change are left out
// while the GPU's queue drains.
void PlanetAutoFrame(double dt, double dFrame)
{
  if (MenuQuality() != 0 || g_fPlanetAuto <= 0.3f)
    return;
  // a stall (a dialog, the window moved) says nothing of the GPU
  if (dt > 0.25)
    return;
  if (g_nPlanetSettle != 0) {
    --g_nPlanetSettle;
    return;
  }
  g_dPlanetSlowS += dt;
  if (++g_nPlanetFrames < 30)
    return;
  if (g_dPlanetSlowS / g_nPlanetFrames > dFrame * 1.2) {
    g_fPlanetAuto = g_fPlanetAuto * 0.8f < 0.3f ? 0.3f : g_fPlanetAuto * 0.8f;
    g_nPlanetSettle = 10;
  }
  g_dPlanetSlowS = 0.0;
  g_nPlanetFrames = 0;
}

unsigned g_nFrameNo;  // the frames drawn, which a dialog's first is told by

// Whether Hyperion's menus are on: the setting, unless HYPERION_FRONT says otherwise, read once.
bool FrontOn(void)
{
  if (!g_bChecked) {
    g_bChecked = true;
    double dMenus = 1.0;
    PortSettingNumber("hyperion", "menus", &dMenus);
    const char *pszFront = SDL_getenv("HYPERION_FRONT");
    g_bOn = pszFront != NULL && pszFront[0] != '\0' ? pszFront[0] == '1' : dMenus != 0.0;
  }
  return g_bOn;
}

// A window message into the frame's input (g_mouseX and the rest), for the menus' loop and for
// the game menu over a game (port_front_gamemenu.cpp): true when it is the kit's, the mouse, a
// key or a character; Alt+Enter stays the window's. A mouse message the window ignores
// (PortMouseIgnored) is the caller's to leave out.
bool FrontInputMsg(void *hWnd, unsigned int uMsg, unsigned long long wParam, long long lParam)
{
  if (uMsg >= 0x200 && uMsg <= 0x20A) {
    if (uMsg == 0x20A)  // WM_MOUSEWHEEL; its position is the screen's, not the client's
      g_nWheel += static_cast<short>((wParam >> 16) & 0xFFFF);
    if (uMsg != 0x20A) {
      g_mouseX = static_cast<float>(static_cast<short>(lParam & 0xFFFF));
      g_mouseY = static_cast<float>(static_cast<short>((lParam >> 16) & 0xFFFF));
    }
    if (uMsg == 0x201) {          // WM_LBUTTONDOWN
      g_bDown = true;
      g_bPressed = true;
      WinMsgSetCapture(hWnd);
    } else if (uMsg == 0x202) {   // WM_LBUTTONUP
      g_bDown = false;
      g_bReleased = true;
      WinMsgReleaseCapture();
    }
    return true;
  }
  if (uMsg == 0x100 || uMsg == 0x104) {  // WM_KEYDOWN, WM_SYSKEYDOWN
    if (uMsg == 0x104 && wParam == VK_RETURN)
      return false;  // Alt+Enter stays the window's
    // a held key counts once, but Backspace repeats in an edit box
    if ((lParam & 0x40000000) == 0 || wParam == VK_BACK)
      g_keys.push_back(static_cast<int>(wParam));
    return true;
  }
  // WM_CHAR, for the edit boxes: printable ASCII, and the bytes of UTF-8 (WinMsgTypeUtf8)
  if (uMsg == 0x102 && ((wParam >= 0x20 && wParam < 0x7F) || (wParam >= 0x80 && wParam <= 0xF4)))
    g_typed.push_back(static_cast<char>(wParam));
  if (uMsg == 0x101 || uMsg == 0x102 || uMsg == 0x105 || uMsg == 0x106)
    return !(uMsg == 0x106 && wParam == VK_RETURN);
  return false;
}

// What every frame of the kit's starts with, before its keys and its layout: the glow, the last
// frame's text and draws let go, and the element a press went down on forgotten when the button
// went down again.
void FrameBegin(void)
{
  WinMsgTypeUtf8();
  UiSetGlow(kScGlow);
  g_strings.clear();
  g_draws.clear();
  g_customs.clear();
  if (g_bPressed)
    g_nPressedOn = 0;
}

// And what it ends with, once laid out: the frame's input used up.
void FrameEnd(void)
{
  g_bPressed = g_bReleased = false;
  g_keys.clear();
  g_typed.clear();
  g_nWheel = 0;
  ++g_nFrameNo;
}

// The standard dialog (StandardDialog): a title, a message and its buttons, over a blackout.
// Returns the button clicked, -1 none. Its keys count from the frame after it shows, so the
// key that opened it does not also answer it.
int ScDialog(const char *pszTitle, const char *pszBody, const char *const *ppszButtons, const char *pszKeys, int nButtons)
{
  static unsigned s_nLast = ~0u;
  const bool bFirst = s_nLast + 1 != g_nFrameNo;
  s_nLast = g_nFrameNo;
  Decl scrim;
  scrim.d.id = Id("dlgscrim");
  scrim.d.layout.sizing.width = Fixed(static_cast<float>(UiWidth()));
  scrim.d.layout.sizing.height = Fixed(static_cast<float>(UiHeight()));
  scrim.d.backgroundColor = Col(kScBlackout);
  scrim.d.floating.attachTo = CLAY_ATTACH_TO_ROOT;
  scrim.d.floating.zIndex = 100;
  Open(scrim);
  Close();
  Decl d;
  d.d.id = Id("dlgbox");
  d.d.layout.layoutDirection = CLAY_TOP_TO_BOTTOM;
  d.d.layout.sizing.width = Fixed(Sc(860));
  d.d.layout.padding = Pad(Fs(60), Fs(60), Fs(40), Fs(38));
  d.d.layout.childAlignment.x = CLAY_ALIGN_X_CENTER;
  d.d.floating.attachTo = CLAY_ATTACH_TO_ROOT;
  d.d.floating.attachPoints.element = CLAY_ATTACH_POINT_CENTER_TOP;
  d.d.floating.attachPoints.parent = CLAY_ATTACH_POINT_CENTER_TOP;
  d.d.floating.offset.y = Sc(400);
  d.d.floating.zIndex = 101;
  static UiCustom frame = {0.0f, DrawScFrame, const_cast<UiColor *>(&kScFillSolid)};
  d.d.custom.customData = &frame;
  Open(d);
  Text(Keep(Upper(pszTitle)), UI_FACE_WIDE, Fs(24), 0.04f, kScInkHi, -1, 0.0f, true);
  Decl body;
  body.d.layout.padding = Pad(0, 0, Fs(14), Fs(36));
  Open(body);
  Text(pszBody, UI_FACE_TEXT, Fs(18), 0.0f, kScInkMsg, -1, 1.4f, true);
  Close();
  Decl row;
  row.d.layout.childGap = Gap(Fs(10));
  Open(row);
  int nChosen = -1;
  for (int k = 0; k < nButtons; ++k) {
    Decl b;
    b.d.id = Id("dlgbtn", k);
    b.d.layout.sizing.width = Fit();
    b.d.layout.sizing.height = Fixed(Sc(52));
    if (ScButton(b, ppszButtons[k], pszKeys[k], k == 0))
      nChosen = k;
  }
  Close();
  Close();
  for (int k = 0; k < nButtons && nChosen < 0 && !bFirst; ++k)
    if (pszKeys[k] != ' ' && Key(pszKeys[k]))
      nChosen = k;
  return nChosen;
}

// The Menu's keys while it is open, on any screen: it has them all, and shuts on Escape, F10
// or B; an entry's key does what a click does.
void MenuKeys(void)
{
  if (Key(VK_ESCAPE) || Key(VK_F10) || Key('B')) {
    g_bMenuOpen = false;
    return;
  }
  for (const NavEntry &e : kDrop)
    if (e.pszId != NULL && Key(e.cKey)) {
      Choose(e.pszId);
      return;
    }
}

void Keys(void)
{
  if (g_nConfirm != CONFIRM_NONE)
    return;  // the dialog reads its own
  if (ProfileKeys())
    return;
  if (g_bMenuOpen) {
    MenuKeys();
    return;
  }
  // Settings and Patch Notes, over any screen (Choose)
  if (NotesUp()) {
    NotesKeys();
    return;
  }
  if (g_bSettings) {
    SettingsKeys();
    return;
  }
  if (g_nScreen == SCREEN_SKIRMISH) {
    SkirmishKeys();
    return;
  }
  if (g_nScreen == SCREEN_CAMPAIGN) {
    CampaignKeys();
    return;
  }
  if (g_nScreen == SCREEN_REPLAYS) {
    ReplaysKeys();
    return;
  }
  if (g_nScreen == SCREEN_BENCH) {
    if (Key(VK_F10)) {
      g_bMenuOpen = true;
      return;
    }
    BenchKeys();
    return;
  }
  if (g_nScreen == SCREEN_TRAINER) {
    if (Key(VK_F10)) {
      g_bMenuOpen = true;
      return;
    }
    TrainerKeys();
    return;
  }
  if (g_nScreen == SCREEN_LAB) {
    if (Key(VK_F10)) {
      g_bMenuOpen = true;
      return;
    }
    LabKeys();
    return;
  }
  if (g_nScreen == SCREEN_LAN) {
    if (Key(VK_F10)) {
      g_bMenuOpen = true;
      return;
    }
    LanKeys();
    return;
  }
  if (g_nScreen == SCREEN_SCORE) {
    if (Key(VK_F10)) {
      g_bMenuOpen = true;
      return;
    }
    ScoreKeys();
    return;
  }
  if (g_bMods) {
    ModsKeys();
    return;
  }
  if (Key(VK_F10)) {
    g_bMenuOpen = true;
    return;
  }
  if (Key(VK_ESCAPE)) {
    Choose("exit");
    return;
  }
  if (Key('N')) {
    g_bNewsOpen = !g_bNewsOpen;
    return;
  }
  if (Key('Y')) {
    CardToggle();
    return;
  }
  if (Key('X')) {
    Choose("exit");
    return;
  }
  for (const NavEntry &n : kNav)
    if (Key(n.cKey) && strcmp(n.pszId, "menu") != 0) {
      NavPick(n.pszId);
      return;
    }
}

// The bar's choice, by a click or a key: another section, or home, goes on once what the screen
// shows has faded out (SwitchIn's first half, kOutS); the rest, and a section that wants a profile
// first, at once (Choose).
const float kOutS = 0.12f;

void NavPick(const char *pszId)
{
  const bool bSection = strcmp(pszId, "campaign") == 0 || strcmp(pszId, "skirmish") == 0 ||
                        strcmp(pszId, "replays") == 0 || strcmp(pszId, "multiplayer") == 0 ||
                        strcmp(pszId, "menu") == 0;
  const bool bGated = strcmp(pszId, "multiplayer") != 0 && strcmp(pszId, "menu") != 0 && ProfileCurrent().empty();
  if (!bSection || bGated || !Enabled(pszId) || strcmp(pszId, ScreenId()) == 0 || g_nScreen == SCREEN_SCORE ||
      g_fLeave >= 0.0f || g_fOut >= 0.0f) {
    if (g_fOut < 0.0f)
      Choose(pszId);
    return;
  }
  g_bMenuOpen = false;
  g_pszOut = pszId;
  g_fOut = 0.0f;
}

// The entrance's motions for this frame (front.css 1371-1381, 1200; front.js 2479): the
// nav bar drops 30 units into place as it shows (0.25 s at 0.9 s), the game's name and the
// world's fade in (0.2 s at 1.2 s), and the notes slide in from the right one after another
// (0.3 s each, 0.12 s apart, from 1.75 s; from 0.25 s on a later visit). The planet's own
// 1.1 s out of black is the world's (WorldIntroBegin).
void Enter(void)
{
  if (g_fEnter < 0.0f)
    return;
  const float t = g_fEnter;
  UiMotion m = {1.0f, 0.0f, 0.0f, 1.0f};
  if (g_bEnterAll) {
    const float fBar = UiSpan(t, 0.9f, 0.25f);
    m.fAlpha = fBar;
    m.fDy = -UiPx(30) * (1.0f - fBar);
    UiMotionSet(Id("bar").id, m);
    m.fDy = 0.0f;
    m.fAlpha = UiSpan(t, 1.2f, 0.2f);
    UiMotionSet(Id("hero").id, m);
    UiMotionSet(Id("plate").id, m);
    // the avatar pops (0.5, 1.2, then 1 over 0.3 s at 1.4 s), the card and the taskbar
    // fade in at 1.5 s (front.css 1375-1376, 1381)
    const float fPop = UiSpan(t, 1.4f, 0.3f);
    UiMotion pop = {fPop, 0.0f, 0.0f, fPop < 0.5f ? 0.5f + 1.4f * fPop : 1.2f - 0.4f * (fPop - 0.5f)};
    UiMotionSet(Id("avatar").id, pop);
    m.fAlpha = UiSpan(t, 1.5f, 0.2f);
    UiMotionSet(Id("cardtab").id, m);
    UiMotionSet(Id("taskclock").id, m);
    UiMotionSet(Id("taskmenu").id, m);
  }
  const float fFirst = g_bEnterAll ? 1.75f : 0.25f;
  int nShown = 0;
  float fLast = 0.0f;
  const auto Slide = [&](unsigned int nId) {
    fLast = fFirst + 0.12f * nShown++;
    const float e = UiEaseEven(UiSpan(t, fLast, 0.3f));
    UiMotion n = {e, Sc(412 + 15) * (1.0f - e), 0.0f, 1.0f};
    UiMotionSet(nId, n);
  };
  for (int i = 0; i < NotesToasts(); ++i)
    if (!NotesToastGone(i))
      Slide(Id("note", i).id);
  // Clear all comes in after the last note it clears, not before the first
  if (NotesLeft() > 1)
    Slide(Id("clearall").id);
  if (t > fLast + 0.3f && t > (g_bEnterAll ? 1.4f : 0.0f))
    g_fEnter = -1.0f;
}

// The return from a game into the screen it came from, skirmish or replays (the user, 2026-10-14):
// the screen comes out of black (0.6 s), the nav bar and the profile card slide down from
// above as the taskbar slides up from below, each fading in (0.45 s from 0.2 s), and the
// screen's panel fades in between them (0.35 s from 0.35 s). A press or key brings it all in.
void Return(void)
{
  if (g_fReturn < 0.0f)
    return;
  const float t = g_fReturn;
  const float e = UiEase(UiSpan(t, 0.2f, 0.45f));
  UiMotion top = {e, 0.0f, -UiPx(90) * (1.0f - e), 1.0f};
  UiMotionSet(Id("bar").id, top);
  UiMotionSet(Id("avatar").id, top);
  UiMotionSet(Id("cardtab").id, top);
  UiMotion bottom = {e, 0.0f, UiPx(90) * (1.0f - e), 1.0f};
  UiMotionSet(Id("taskclock").id, bottom);
  UiMotionSet(Id("taskmenu").id, bottom);
  UiMotion panel = {UiEase(UiSpan(t, 0.35f, 0.35f)), 0.0f, 0.0f, 1.0f};
  UiMotionSet(Id(g_nScreen == SCREEN_REPLAYS ? "rppanel" : g_nScreen == SCREEN_BENCH ? "bmpanel"
                 : g_nScreen == SCREEN_TRAINER ? "trpanel" : g_nScreen == SCREEN_LAN ? LanPanelId()
                 : g_nScreen == SCREEN_LAB ? LabPanelId() : "skpanel").id,
              panel);
  if (t > 0.75f)
    g_fReturn = -1.0f;
}

void ReturnBlack(int nWidth, int nHeight)
{
  if (g_fReturn >= 0.0f)
    UiFill(0.0f, 0.0f, static_cast<float>(nWidth), static_cast<float>(nHeight),
           UiRgb(0x000000, 1.0f - UiEase(UiSpan(g_fReturn, 0.0f, 0.6f))));
}

// A section chosen from another, by the bar or a screen's own button (the user, 2026-10-26: the
// switch felt "like a webpage changing a page"): what the screen shows fades in and rises 10
// units into place (0.25 s), and, chosen from the bar, first fades out (kOutS, NavPick), while
// the nav bar, the profile's card, the taskbar and the world stay as they are; between the two,
// 1.17's screens pass behind the last frame (FrontHandBack). Not as the entrance plays, out of
// black after a game, or while leaving. Drawn after the layout, which may have changed the
// screen this frame.
const float kSwitchS = 0.25f;

void SwitchIn(void)
{
  if (g_nScreen != g_nScreenShown) {
    if (g_nScreenShown >= 0 && g_fReturn < 0.0f && g_fLeave < 0.0f)
      g_fSwitch = 0.0f;
    g_nScreenShown = g_nScreen;
    g_fOut = -1.0f;
  }
  if ((g_fSwitch < 0.0f && g_fOut < 0.0f) || g_fReturn >= 0.0f || g_fLeave >= 0.0f)
    return;
  float a = 1.0f, dy = 0.0f;
  if (g_fOut >= 0.0f) {
    a = 1.0f - UiEaseEven(UiSpan(g_fOut, 0.0f, kOutS));
  } else {
    a = UiEase(UiSpan(g_fSwitch, 0.0f, kSwitchS));
    dy = UiPx(10) * (1.0f - a);
  }
  UiMotionSet(Id("root").id, UiMotion{a, 0.0f, dy, 1.0f});
  // the frame round it stays: a motion of their own, the smaller box, wins over the root's
  static const char *const kStill[] = {"bar", "avatar", "cardtab", "taskclock", "taskmenu"};
  for (const char *pszId : kStill)
    UiMotionSet(Id(pszId).id, UiMotion{1.0f, 0.0f, 0.0f, 1.0f});
  if (g_fSwitch >= kSwitchS)
    g_fSwitch = -1.0f;
}

// Whether a choice starts a game, or a mission's briefing: the screen goes to black first.
bool Launches(int nChoice)
{
  if (nChoice == PORT_FRONT_MODE || nChoice == FRONT_SKIRMISH_STARTED || nChoice == FRONT_REPLAY_STARTED ||
      nChoice == FRONT_LOAD_STARTED)
    return true;
  if (nChoice == FRONT_CAMPAIGN_DONE) {
    const int nGlues = CampaignGluesMode();
    return nGlues == 7 || nGlues == 8 || nGlues == 9 || CampaignWatching();
  }
  return false;
}

// The leaving's motions: the nav bar goes up 30 units as it fades and the rest fades with
// it (0.25 s), then black comes over all of it (0.8 s from 0.3 s, on the design's ease).
// Drawn after the layout; the choice goes on at 1.1 s.
void Leave(void)
{
  if (g_fLeave < 0.0f)
    return;
  const float s = UiSpan(g_fLeave, 0.0f, 0.25f);
  UiMotion m = {1.0f - s, 0.0f, -UiPx(30) * s, 1.0f};
  UiMotionSet(Id("bar").id, m);
  m.fDy = 0.0f;
  UiMotionSet(Id("root").id, m);
}

void LeaveBlack(int nWidth, int nHeight)
{
  if (g_fLeave >= 0.0f)
    UiFill(0.0f, 0.0f, static_cast<float>(nWidth), static_cast<float>(nHeight),
           UiRgb(0x000000, UiEase(UiSpan(g_fLeave, 0.3f, 0.8f))));
}

// Any press or key brings the whole entrance in at once.
void EnterSkip(void)
{
  g_fEnter = -1.0f;
  WorldIntroSkip();
}

void Home(void)
{
  Decl root;
  root.d.id = Id("root");
  root.d.layout.sizing.width = Grow();
  root.d.layout.sizing.height = Grow();
  root.d.layout.layoutDirection = CLAY_TOP_TO_BOTTOM;
  Open(root);
  NavBar();
  Profile();
  Taskbar();
  if (g_nScreen == SCREEN_SKIRMISH || g_nScreen == SCREEN_CAMPAIGN || g_nScreen == SCREEN_REPLAYS ||
      g_nScreen == SCREEN_BENCH || g_nScreen == SCREEN_TRAINER || g_nScreen == SCREEN_LAN || g_nScreen == SCREEN_LAB ||
      g_nScreen == SCREEN_SCORE) {
    if (g_nScreen == SCREEN_SKIRMISH)
      SkirmishScreen();
    else if (g_nScreen == SCREEN_SCORE)
      ScoreScreen();
    else if (g_nScreen == SCREEN_REPLAYS)
      ReplaysScreen();
    else if (g_nScreen == SCREEN_BENCH)
      BenchScreen();
    else if (g_nScreen == SCREEN_TRAINER)
      TrainerScreen();
    else if (g_nScreen == SCREEN_LAN)
      LanScreen();
    else if (g_nScreen == SCREEN_LAB)
      LabScreen();
    else
      CampaignScreen();
    ProfileScreen();
    Return();
    if (g_bMenuOpen)
      MenuPanel();
    if (g_bSettings)
      SettingsDialog();
    if (NotesUp())
      NotesDialog();
    if (g_nConfirm != CONFIRM_NONE)
      Confirm();
    Close();
    return;
  }
  Hero();
  Plate();
  News();
  ProfileScreen();
  Enter();
  if (g_bMenuOpen)
    MenuPanel();
  if (g_bSettings)
    SettingsDialog();
  if (g_bMods)
    ModsDialog();
  if (NotesUp())
    NotesDialog();
  if (g_nConfirm != CONFIRM_NONE)
    Confirm();
  Close();
}

void LogState(void)
{
  static std::string s_last;
  char sz[160];
  SDL_snprintf(sz, sizeof sz, "menu %s, notifications %s, %d notes, mod %s, mods %s%s, settings %s%s%s, tip %d",
               g_bMenuOpen ? "open" : "shut", g_bNewsOpen ? "open" : "folded", NotesLeft(), ActiveMod().name.c_str(),
               g_bMods ? "open on " : "shut", g_bMods ? PortMods()[g_nModPick].name.c_str() : "",
               g_bSettings ? "open on " : "shut", g_bSettings ? kTabs[g_nTab].pszId : "",
               g_nConfirm == CONFIRM_RESTORE ? ", asking to restore"
               : g_nConfirm == CONFIRM_RESTART || g_nConfirm == CONFIRM_MOD_RESTART ? ", restart needed" : "", g_nTip);
  if (g_nScreen != SCREEN_HOME)
    SDL_snprintf(sz, sizeof sz, "%s", ScreenId());
  if (s_last != sz) {
    s_last = sz;
    FrontLog("front: %s\n", sz);
  }
}

bool EnvOn(const char *pszName)
{
  const char *p = SDL_getenv(pszName);
  return p != NULL && p[0] == '1';
}

// The screen's frames until it chooses: g_nChoice. HYPERION_FRONT_SNAP saves a frame a while
// in (HYPERION_FRONT_SNAP_AT seconds, 2 by default) on the screen HYPERION_FRONT_SNAP_SCREEN
// names ("home" by default), and HYPERION_FRONT_EXIT=1 then exits.
void RunScreen(int nWindowW, int nWindowH, float fRefresh)
{
  const char *pszSnap = SDL_getenv("HYPERION_FRONT_SNAP");
  const char *pszSnapAt = SDL_getenv("HYPERION_FRONT_SNAP_AT");
  const double dSnapAt = pszSnapAt != NULL ? SDL_atof(pszSnapAt) : 2.0;
  const char *pszSnapScreen = SDL_getenv("HYPERION_FRONT_SNAP_SCREEN");
  if (pszSnapScreen == NULL || pszSnapScreen[0] == '\0')
    pszSnapScreen = "home";
  if (strcmp(pszSnapScreen, g_nScreen == SCREEN_HOME ? "home" : ScreenId()) != 0)
    pszSnap = NULL;
  const bool bExitAfter = EnvOn("HYPERION_FRONT_EXIT");
  bool bSnapped = false;
  g_pszOut = NULL;
  g_fOut = -1.0f;
  // how long 1.17's screens took between two of the front end's (FrontHandBack)
  if (g_qwHandedBack != 0) {
    FrontLog("front: back from 1.17's screens in %.1f ms\n", (SDL_GetTicksNS() - g_qwHandedBack) / 1e6);
    g_qwHandedBack = 0;
  }
  // the game mode the screen runs in: 4, the menus', or after a game the score screen's 6 or 7;
  // close_window changes it
  const int nMode = gwGameMode;

  g_bUp = true;
  g_bDown = g_bPressed = g_bReleased = false;
  g_keys.clear();
  g_typed.clear();

  const double dFrame = 1.0 / (fRefresh > 20.0f ? fRefresh : 60.0f);
  Uint64 nLast = SDL_GetPerformanceCounter();
  const Uint64 nStart = nLast;
  const double dFreq = static_cast<double>(SDL_GetPerformanceFrequency());
  unsigned int nFrames = 0;
  unsigned int nFailed = 0;
  // where a frame's time goes, for the closing line: waiting for the swapchain's image, the
  // layout on the processor, and the GPU's submit and present
  double dWaitS = 0.0, dLayoutS = 0.0, dSubmitS = 0.0, dSlowestS = 0.0;
  unsigned int nShown = 0;
  while (g_nChoice == 0) {
    // the window's messages, which port_winmsg.cpp makes of SDL's events
    WinMsg msg;
    while (WinMsgPeek(&msg)) {
      if (msg.uMsg == WM_QUIT) {
        g_nChoice = 2;
        break;
      }
      WinMsgTranslate(&msg);
      WinMsgDispatch(&msg);
    }
    // the window was closed (close_window)
    if (gwGameMode != nMode && g_nChoice == 0) {
      g_nChoice = 0xFFF0;
      break;
    }
    // the live channel (port_control.cpp): its messages, and the canvas when a tool asks
    PortControlPoll();
    PortLabFarmPump();  // the AI lab's farm, its workers' games
    PortDiscordPump();  // what the player does, to Discord
    // a LAN room's match, made or joined: 1.17's chat room next, once the screen has
    // gone to black as for a skirmish's start; the launch wins over a choice already leaving
    if (LanFrontFrame()) {
      g_nLeaving = PORT_FRONT_LAN;
      if (g_fLeave < 0.0f) {
        g_fLeave = 0.0f;
        EnterSkip();
      }
    }
    if (g_nScreen == SCREEN_HOME && LanJoinedNow()) {
      g_nScreen = SCREEN_LAN;
      WorldSetView("campaign", false);
      LanOpen();
    }
    if (PortControlFrontCanvas(&g_frontCanvas) || !g_frontCanvas.empty() || !g_frontMap.empty()) {
      g_nChoice = PORT_FRONT_MODE;
      break;
    }
    const Uint64 nNow = SDL_GetPerformanceCounter();
    const double dt = (nNow - nLast) / dFreq;
    const double dSince = (nNow - nStart) / dFreq;
    nLast = nNow;
    g_fFrameDt = static_cast<float>(dt > 0.1 ? 0.1 : dt);
    WorldStep(dt);
    if (const char *pszThen = ProfileThen())
      Choose(pszThen);
    if (ProfileChanged() && g_nScreen == SCREEN_CAMPAIGN)
      CampaignReread();
    if (g_fEnter >= 0.0f) {
      if (g_bPressed || !g_keys.empty() || g_nScreen != SCREEN_HOME)
        EnterSkip();
      else
        g_fEnter += static_cast<float>(dt);
    }
    if (g_fReturn >= 0.0f)
      g_fReturn = g_bPressed || !g_keys.empty() ? -1.0f : g_fReturn + static_cast<float>(dt);
    // the switch's fades go on by a frame's time, but at most a 30th of a second a frame, so a
    // slow frame as a screen first draws does not take them in one step
    const float fStep = static_cast<float>(dt < 1.0 / 30.0 ? dt : 1.0 / 30.0);
    if (g_fSwitch >= 0.0f)
      g_fSwitch += fStep;
    // the bar's choice, once the screen has faded out; it takes no input meanwhile, and the
    // screen stays faded out until the next one draws (SwitchIn)
    if (g_fOut >= 0.0f && g_pszOut != NULL) {
      g_fOut += fStep;
      g_bDown = g_bPressed = g_bReleased = false;
      g_keys.clear();
      g_typed.clear();
      if (g_fOut >= kOutS) {
        const char *pszOut = g_pszOut;
        g_pszOut = NULL;
        Choose(pszOut);
        // nothing came of it: the screen comes back in
        if (g_nChoice == 0 && g_nScreen == g_nScreenShown) {
          g_fOut = -1.0f;
          g_fSwitch = 0.0f;
        }
      }
    }
    // leaving: the screen takes no more input, and goes on once it is black
    if (g_fLeave >= 0.0f) {
      g_fLeave += static_cast<float>(dt);
      g_bDown = g_bPressed = g_bReleased = false;
      g_keys.clear();
      g_typed.clear();
      if (g_fLeave >= 1.1f) {
        g_nChoice = g_nLeaving;
        g_fLeave = -1.0f;
        break;
      }
    }

    PortPresentClientSize(&nWindowW, &nWindowH);
    int nWidth = 0;
    int nHeight = 0;
    const bool bTrace = nFrames < 2;
    if (bTrace)
      FrontLog("frame %u: begin\n", nFrames);
    const Uint64 nBegin = SDL_GetPerformanceCounter();
    const bool bBegun = PortGpuUiBegin(nWindowW, nWindowH, &nWidth, &nHeight);
    const Uint64 nBegun = SDL_GetPerformanceCounter();
    if (!bBegun) {
      ++nFailed;
      Keys();
      g_keys.clear();
      g_typed.clear();
      if (nFrames == 0 && nFailed >= 10) {
        FrontLog("front: cannot draw (%s); 1.17's menu runs\n", PortGpuUiError());
        PortStartupStep("menus", "Hyperion's menus cannot draw (%s); 1.17's menus run", PortGpuUiError());
        g_nChoice = -1;
        g_bOn = false;
        break;
      }
      SDL_Delay(5);
      continue;
    }
    {
      if (bTrace)
        FrontLog("frame %u: %dx%d\n", nFrames, nWidth, nHeight);
      UiBegin(nWidth, nHeight);
      FrameBegin();
      UiPlanet(0.0f, 0.0f, static_cast<float>(nWidth), static_cast<float>(nHeight));
      Keys();
      if (bTrace)
        FrontLog("frame %u: layout\n", nFrames);
      UiLayoutBegin(g_mouseX, g_mouseY, g_bDown);
      LayoutCheckBegin();
      Home();
      if (g_nScreen == SCREEN_SKIRMISH && g_nChoice == 0)
        g_nChoice = SkirmishResult();
      else if (g_nScreen == SCREEN_CAMPAIGN && g_nChoice == 0)
        g_nChoice = CampaignResult();
      else if (g_nScreen == SCREEN_REPLAYS && g_nChoice == 0)
        g_nChoice = ReplaysResult();
      else if (g_nScreen == SCREEN_BENCH && g_nChoice == 0) {
        const int nBench = BenchResult();
        if (nBench == FRONT_BENCH_HOME) {
          g_nScreen = SCREEN_HOME;
          WorldSetView("home", false);
        } else if (nBench == FRONT_BENCH_STARTED) {
          g_nChoice = PORT_FRONT_MODE;
        }
      } else if (g_nScreen == SCREEN_LAN && g_nChoice == 0) {
        const int nLan = LanResult();
        if (nLan == FRONT_LAN_HOME) {
          g_nScreen = SCREEN_HOME;
          WorldSetView("home", false);
        } else if (nLan == FRONT_LAN_ORIGINAL) {
          g_bPassProfile = false;
          g_nChoice = 4;
        } else if (nLan == FRONT_LAN_WATCH) {
          g_nChoice = PORT_FRONT_MODE;  // a match watched, started as a mode's game
        }
      } else if (g_nScreen == SCREEN_TRAINER && g_nChoice == 0) {
        const int nTrainer = TrainerResult();
        if (nTrainer == FRONT_TRAINER_HOME) {
          g_nScreen = SCREEN_HOME;
          WorldSetView("home", false);
        } else if (nTrainer == FRONT_TRAINER_STARTED) {
          g_nChoice = PORT_FRONT_MODE;
        }
      } else if (g_nScreen == SCREEN_SCORE && g_nChoice == 0) {
        if (ScoreResult() == FRONT_SCORE_DONE) {
          if (g_bScoreFromHome) {
            ScoreClose();
            g_nScreen = SCREEN_HOME;
            WorldSetView("home", false);
          } else {
            g_nChoice = FRONT_SCORE_DONE;
          }
        }
      } else if (g_nScreen == SCREEN_LAB && g_nChoice == 0) {
        const int nLab = LabResult();
        if (nLab == FRONT_LAB_HOME) {
          g_nScreen = SCREEN_HOME;
          WorldSetView("home", false);
        } else if (nLab == FRONT_LAB_WATCH) {
          g_nChoice = PORT_FRONT_MODE;  // a game of the farm watched, started as a mode's game
        }
      }
      LayoutCheckEnd(ScreenId());
      if (bTrace)
        FrontLog("frame %u: laid out\n", nFrames);
      if (g_fLeave < 0.0f && Launches(g_nChoice)) {
        g_nLeaving = g_nChoice;
        g_fLeave = 0.0f;
        EnterSkip();
      }
      // the screens give their result again each frame: it waits until the screen is black
      if (g_fLeave >= 0.0f)
        g_nChoice = 0;
      Leave();
      SwitchIn();
      UiLayoutEnd();
      ReturnBlack(nWidth, nHeight);
      LeaveBlack(nWidth, nHeight);
      LogState();
      if (bTrace)
        FrontLog("frame %u: drawn\n", nFrames);
      PlanetParams params;
      WorldParams(nWidth, nHeight, &params);
      int nPrims = 0;
      const UiPrim *pPrims = UiPrims(&nPrims);
      // the planet at no more than 1440 lines: it is soft, and the shader is the frame's cost;
      // fewer with Menu background quality's Low, or Auto on a card that cannot keep up
      const float fPlanetScale = PlanetScale(nHeight);
      std::string toolSnap;
      const bool bToolSnap = PortControlFrontSnap(&toolSnap);
      const bool bSnap = !bToolSnap && pszSnap != NULL && !bSnapped && dSince >= dSnapAt;
      const Uint64 nLaidOut = SDL_GetPerformanceCounter();
      if (PortGpuUiShown())
        ++nShown;
      const bool bSaved = PortGpuUiEnd(nWidth, nHeight, &params, fPlanetScale, pPrims, nPrims,
                                       bToolSnap ? toolSnap.c_str() : bSnap ? pszSnap : NULL);
      const Uint64 nEnded = SDL_GetPerformanceCounter();
      dWaitS += (nBegun - nBegin) / dFreq;
      dLayoutS += (nLaidOut - nBegun) / dFreq;
      dSubmitS += (nEnded - nLaidOut) / dFreq;
      if ((nEnded - nBegin) / dFreq > dSlowestS && nFrames >= 2)
        dSlowestS = (nEnded - nBegin) / dFreq;
      if (bToolSnap)
        PortControlFrontSnapped(bSaved, nWidth, nHeight);
      if (bTrace)
        FrontLog("frame %u: presented, %d primitives, %.1f ms\n", nFrames, nPrims,
                 (nEnded - nBegin) * 1000.0 / dFreq);
      PortStartupFrame();
      if (bSnap) {
        bSnapped = true;
        FrontLog("front: snapshot %s at %.2f s, %dx%d, %d primitives\n", pszSnap, dSince, nWidth, nHeight, nPrims);
        if (bExitAfter)
          Choose("exit");
      }
      ++nFrames;
    }
    FrameEnd();

    if (nFrames > 2)
      PlanetAutoFrame(dt, dFrame);

    // the display's pace
    const double dSpent = (SDL_GetPerformanceCounter() - nNow) / dFreq;
    if (dSpent < dFrame)
      SDL_DelayPrecise(static_cast<Uint64>((dFrame - dSpent) * 1e9));
  }
  g_bUp = false;
  FrontLog("front: %u frames in %.1f s; chose %d\n", nFrames, (SDL_GetPerformanceCounter() - nStart) / dFreq, g_nChoice);
  if (nFrames != 0)
    FrontLog("front: %u of the frames shown; a frame %.2f ms waiting for the swapchain, %.2f ms laying out, %.2f ms "
        "submitting; slowest %.1f ms; planet at %.0f%%\n", nShown, dWaitS * 1000.0 / nFrames,
        dLayoutS * 1000.0 / nFrames, dSubmitS * 1000.0 / nFrames, dSlowestS * 1000.0,
        PlanetScale(nWindowH) * 100.0);
}

}  // namespace Front

using namespace Front;

// ---------------------------------------------------------------------------

int PortFrontActive(void)
{
  return g_bUp;
}

const char *PortFrontScreen(void)
{
  return ScreenId();
}

int PortFrontWndMsg(void *hWnd, unsigned int uMsg, UINT_PTR wParam, LONG_PTR lParam, LONG_PTR *plResult)
{
  if (!g_bUp)
    return 0;
  UINT_PTR w = wParam;
  if (uMsg >= 0x200 && uMsg <= 0x20A && PortMouseIgnored(&w)) {
    *plResult = 0;
    return 1;
  }
  if (!FrontInputMsg(hWnd, uMsg, wParam, lParam))
    return 0;
  *plResult = 0;
  return 1;
}

// After Campaign, Skirmish or Replays on the home screen, 1.17's profile screen is passed as
// its OK would pass it when the profile chosen last (the settings file's "hyperion",
// "profile") still has its character file: TRUE, once, with that profile the local
// player's. The campaign screen's Change Profile shows the screen.
int PortFrontPassProfile(void)
{
  const bool bPass = g_bPassProfile;
  g_bPassProfile = false;
  if (!bPass || !g_bOn)
    return FALSE;
  char szName[32] = "";
  if (!PortSettingString("hyperion", "profile", szName, sizeof szName) || szName[0] == '\0')
    return FALSE;
  unsigned char rec[0x70];
  if (!ReadCharacterFile(szName, rec))
    return FALSE;
  SetLocalPlayerName(szName);
  FrontLog("front: profile %s, the screen passed\n", szName);
  return TRUE;
}

void PortFrontProfileChosen(const char *pszName)
{
  PortSettingSetString("hyperion", "profile", pszName);
}

int FrontKit::FrontDialog(const char *pszTitle, const char *pszBody, const char *const *ppszButtons,
                          const char *pszKeys, int nButtons)
{
  return ScDialog(pszTitle, pszBody, ppszButtons, pszKeys, nButtons);
}

const char *PortFrontAfterProfile(void)
{
  const char *p = g_pszNext;
  g_pszNext = NULL;
  return p;
}

// The front end gives the window back to 1.17's screens. Those load their palette and fade
// in only when the screen differs from the last glue screen shown (glue_screen_activate);
// Hyperion's screens are none, so after a visit to one the screen it left would come back
// with its palette still faded out, black. The last palette is forgotten instead. With
// bComesBack, a screen of the front end's is next (home, or a section home chose), and 1.17's
// screens on the way stay unseen behind this one's last frame (PortPresentFrontKeep).
static void FrontHandBack(bool bComesBack = false)
{
  PortPresentFrontEnd();
  glues_reset_prev_pal();
  if (bComesBack) {
    PortPresentFrontKeep();
    g_qwHandedBack = SDL_GetTicksNS();
  }
}

// A seat of a LAN room (--join) where the front end cannot draw, a browser's: no
// screen, the room's lobby being the page's, until the room launches a match, which goes as
// the front end's does, or has it watch one (W3), whose replay starts as the front end's
// Watch starts it; 0xFFF0, the menu again, when the window closes first.
static int SeatWait(void)
{
#if defined(__EMSCRIPTEN__)
  // the page round the game is told it waits, and goes behind the room's page (web/play)
  EM_ASM({ if (Module['hySay']) Module['hySay']('waiting'); });
  struct Said {
    ~Said() { EM_ASM({ if (Module['hySay']) Module['hySay']('game'); }); }
  } said;
#endif
  Uint64 qwAsked = 0;
  for (;;) {
    WinMsg msg;
    while (WinMsgPeek(&msg)) {
      if (msg.uMsg == WM_QUIT)
        return 0xFFF0;
      WinMsgTranslate(&msg);
      WinMsgDispatch(&msg);
    }
    PortControlPoll();
    if (LanFrontFrame())
      return PORT_FRONT_LAN;
    LanWatchPump();
    if (LanWatchReady())
      return PORT_FRONT_MODE;
    // a match that sent nothing of itself has ended; the room says the next
    if (!LanWatchWaiting())
      qwAsked = 0;
    else if (qwAsked == 0)
      qwAsked = SDL_GetTicks();
    else if (SDL_GetTicks() - qwAsked > 15000)
      LanWatchCancel();
    Sleep(16);
  }
}

int PortFrontMainMenu(int *pbExpansion)
{
  FrontOn();
  if (!PortPresentGpu() && PortCommandOptions()->szJoin[0])
    return SeatWait();
  if (!g_bOn || !PortPresentGpu())
    return -1;
  if (g_bExitNext) {
    g_bExitNext = false;
    *pbExpansion = ActiveMod().bExpansion;
    return 2;
  }
  // a canvas the live channel asked for while another screen ran (the campaign screen a game
  // returns to) starts now: only the main menu's choice starts a mode (gluMain.cpp)
  if (!g_frontCanvas.empty() || !g_frontMap.empty()) {
    *pbExpansion = ActiveMod().bExpansion;
    return PORT_FRONT_MODE;
  }

  int nWindowW = 0;
  int nWindowH = 0;
  float fRefresh = 0.0f;
  PortPresentFrontBegin(&nWindowW, &nWindowH, &fRefresh);
  FrontLog("front: window %dx%d, refresh %.1f\n", nWindowW, nWindowH, fRefresh);
  FrontLog("front: text %s; wide %s; bold %s; numbers %s\n", UiFaceSource(UI_FACE_TEXT), UiFaceSource(UI_FACE_WIDE),
      UiFaceSource(UI_FACE_BOLD), UiFaceSource(UI_FACE_MONO));

  ThemeApply();
  g_bSkirmishGame = g_bReplayGame = false;  // whatever comes next, it is not that game
  g_bScoreFromHome = g_bFromScore = false;
  g_nScreen = SCREEN_HOME;
  g_nChoice = 0;
  g_bMenuOpen = false;
  g_nMod = PortModActive();
  g_bMods = false;
  WorldSetPlanet(ActiveMod().world.c_str(), true);
  if (const char *pszWorld = SDL_getenv("HYPERION_FRONT_WORLD"))
    WorldSetPlanet(pszWorld, true);
  // a section another screen's bar chose (g_pszSwitch): chosen here before a frame of home is
  // drawn, so the bar goes straight from one to the other; a screen of 1.17's next ends the loop
  // at once, and Mods, the credits or the intro shows home as from home
  if (const char *pszSwitch = g_pszSwitch) {
    g_pszSwitch = NULL;
    g_fEnter = -1.0f;
    g_bEntered = true;
    Choose(pszSwitch);
    if (g_nChoice == 0 && g_nScreen == SCREEN_HOME)
      WorldSetView("home", false);
    RunScreen(nWindowW, nWindowH, fRefresh);
    FrontHandBack(g_nChoice == 3 && g_bPassProfile);
    *pbExpansion = ActiveMod().bExpansion;
    return g_nChoice;
  }
  // the camera comes home from where the last screen left it, after the first time
  WorldSetView("home", !g_bEntered);
  // the entrance: the whole of it the first time, unless a check opens a screen or takes a
  // still (HYPERION_FRONT_ENTER=1 plays it anyway, =0 never)
  {
    const char *pszEnter = SDL_getenv("HYPERION_FRONT_ENTER");
    const bool bCheck = SDL_getenv("HYPERION_FRONT_SNAP") != NULL || SDL_getenv("HYPERION_FRONT_OPEN") != NULL ||
                        SDL_getenv("HYPERION_FRONT_SETTINGS") != NULL;
    const bool bPlay = pszEnter != NULL && pszEnter[0] != '\0' ? pszEnter[0] == '1' : !bCheck;
    g_bEnterAll = !g_bEntered;
    g_fEnter = bPlay ? 0.0f : -1.0f;
    if (bPlay && g_bEnterAll)
      WorldIntroBegin();
    g_bEntered = true;
  }
  // HYPERION_FRONT_SETTINGS=<tab> opens Settings on that tab, for a check without a person
  if (const char *pszTab = SDL_getenv("HYPERION_FRONT_SETTINGS")) {
    for (int t = 0; t < kTabCount; ++t)
      if (strcmp(kTabs[t].pszId, pszTab) == 0)
        g_nTab = t;
    SettingsOpen();
  }
  // a benchmark run started from its set-up has ended: back to the set-up, on its runs, or on
  // New Run when the run's Set-up left it
  if (BenchCameBack() && ActiveMod().mode == "bench") {
    g_nScreen = SCREEN_BENCH;
    WorldSetView("campaign", true);
    BenchOpen(PortBenchBackTo() == 1);
    g_fReturn = 0.0f;
    g_fEnter = -1.0f;
  }
  // a trainer session saved as its game ended: back to the trainer's entry, on its sessions
  if (TrainerCameBack() && ActiveMod().mode == "trainer") {
    g_nScreen = SCREEN_TRAINER;
    WorldSetView("campaign", true);
    TrainerOpen(true);
    g_fReturn = 0.0f;
    g_fEnter = -1.0f;
  }
  // a game watched from the lab has ended: back to the lab
  if (LabCameBack() && ActiveMod().mode == "lab") {
    g_nScreen = SCREEN_LAB;
    WorldSetView("campaign", true);
    LabOpen(false);
    g_fReturn = 0.0f;
    g_fEnter = -1.0f;
  }
  // back from a room's match, or still in a room: its console or the room
  if (LanCameBack()) {
    g_nScreen = SCREEN_LAN;
    WorldSetView("campaign", true);
    LanOpen();
    g_fReturn = 0.0f;
    g_fEnter = -1.0f;
  }
  // HYPERION_FRONT_OPEN=skirmish goes on to that screen, once
  static bool s_bOpened;
  if (const char *pszOpen = SDL_getenv("HYPERION_FRONT_OPEN"))
    if (!s_bOpened) {
      s_bOpened = true;
      // bench and bench-runs: the benchmark's set-up, on New Run or its runs, while its mod is
      // the active one
      if ((strcmp(pszOpen, "bench") == 0 || strcmp(pszOpen, "bench-runs") == 0) && ActiveMod().mode == "bench") {
        g_nScreen = SCREEN_BENCH;
        WorldSetView("campaign", true);
        BenchOpen(strcmp(pszOpen, "bench-runs") == 0);
      } else if ((strcmp(pszOpen, "trainer") == 0 || strcmp(pszOpen, "trainer-sessions") == 0) &&
                 ActiveMod().mode == "trainer") {
        // trainer and trainer-sessions: the trainer's entry, on New Session or its sessions
        g_nScreen = SCREEN_TRAINER;
        WorldSetView("campaign", true);
        TrainerOpen(strcmp(pszOpen, "trainer-sessions") == 0);
      } else if ((strcmp(pszOpen, "lab") == 0 || strcmp(pszOpen, "lab-games") == 0) && ActiveMod().mode == "lab") {
        // lab and lab-games: the AI lab, on its farm or its games
        g_nScreen = SCREEN_LAB;
        WorldSetView("campaign", true);
        LabOpen(strcmp(pszOpen, "lab-games") == 0);
      } else if (strncmp(pszOpen, "score-", 6) == 0) {
        // score-<preset>: the score screen with stand-in players (port_score.cpp's ScoreStandIn),
        // for a check of its layouts without playing such a game out
        g_nScreen = SCREEN_SCORE;
        g_bScoreFromHome = true;
        WorldSetView("campaign", true);
        ScoreOpen(ScoreStandIn(pszOpen + 6), true);
      } else if (strcmp(pszOpen, "lanhost") == 0 || strcmp(pszOpen, "lanhost-match") == 0) {
        // lanhost: Host a Room, at once (a check without a person); lanhost-match goes on to a
        // new match's lobby once the console is in (port_front_lan.cpp's LanScreen)
        Choose("multiplayer");
        Lan::ServerStart();
        Lan::ToConsole();
      } else {
        Choose(pszOpen);
      }
    }
  RunScreen(nWindowW, nWindowH, fRefresh);
  FrontHandBack(g_nChoice == 3 && g_bPassProfile);
  *pbExpansion = ActiveMod().bExpansion;
  return g_nChoice;
}

// Skirmish, in Play Custom's place (do_create_dlg, single player): 0x0C when the game is made,
// 0 for home, -1 when the front end is off and Play Custom runs.
int PortFrontSkirmish(int nSpeed)
{
  if (!g_bOn || !PortPresentGpu())
    return -1;
  int nWindowW = 0;
  int nWindowH = 0;
  float fRefresh = 0.0f;
  PortPresentFrontBegin(&nWindowW, &nWindowH, &fRefresh);
  g_nScreen = SCREEN_SKIRMISH;
  g_nChoice = 0;
  g_bMenuOpen = false;
  g_bMods = false;
  g_bSettings = false;
  WorldSetView("campaign", false);
  SkirmishOpen(nSpeed);
  // back from a game: out of black, unless the score screen was up, which it takes over from
  g_fReturn = g_bBackFromGame && !g_bFromScore ? 0.0f : -1.0f;
  g_bBackFromGame = g_bFromScore = false;
  RunScreen(nWindowW, nWindowH, fRefresh);
  g_nScreen = SCREEN_HOME;
  FrontHandBack(g_nChoice == FRONT_SKIRMISH_HOME && !g_bExitNext);
  if (g_nChoice == 2)  // the window's Close, or WM_QUIT
    g_bExitNext = true;
  g_bSkirmishGame = g_nChoice == FRONT_SKIRMISH_STARTED;
  if (g_bSkirmishGame)
    ProfileCountGame();
  return g_bSkirmishGame ? 0x0C : 0;
}

// Replays, in 1.17's replay list's place (do_create_dlg's replay mode): 0x0C when a replay is
// set to play, 0 for home, -1 when the front end is off and 1.17's list runs.
int PortFrontReplays(void)
{
  if (!g_bOn || !PortPresentGpu())
    return -1;
  int nWindowW = 0;
  int nWindowH = 0;
  float fRefresh = 0.0f;
  PortPresentFrontBegin(&nWindowW, &nWindowH, &fRefresh);
  g_nScreen = SCREEN_REPLAYS;
  g_nChoice = 0;
  g_bMenuOpen = false;
  g_bMods = false;
  g_bSettings = false;
  WorldSetView("campaign", false);
  ReplaysOpen();
  g_fReturn = g_bBackFromGame && !g_bFromScore ? 0.0f : -1.0f;
  g_bBackFromGame = g_bFromScore = false;
  RunScreen(nWindowW, nWindowH, fRefresh);
  ReplaysClose();
  // the replay was made ready at Watch; it starts now the screen is black
  g_bReplayGame = g_nChoice == FRONT_REPLAY_STARTED;
  if (g_bReplayGame)
    PortLaunchStart();
  g_nScreen = SCREEN_HOME;
  FrontHandBack(g_nChoice == FRONT_REPLAYS_HOME && !g_bExitNext);
  if (g_nChoice == 2)  // the window's Close, or WM_QUIT
    g_bExitNext = true;
  return g_nChoice == FRONT_REPLAY_STARTED ? 0x0C : 0;
}

// After a game the skirmish screen set up, or a replay the replays screen started, 1.17 goes
// on to its campaign screen, by whichever way the game ended (sub_4BB360, and Quit
// Mission's own); Hyperion goes back to the screen it came from instead (0x0B, Play
// Custom's place, or 0x0C, the replay list's). Once per game.
static int AfterGame(void)
{
  const bool bSkirmish = g_bSkirmishGame && g_bOn;
  const bool bReplay = g_bReplayGame && g_bOn;
  g_bSkirmishGame = g_bReplayGame = false;
  g_bBackFromGame = bSkirmish || bReplay;
  FrontLog("front: a game ended%s\n", bSkirmish ? ", back to the skirmish" : bReplay ? ", back to the replays" : "");
  return bSkirmish ? 0x0B : bReplay ? 0x0C : -1;
}

bool PortFrontSkirmishGame(void)
{
  return g_bSkirmishGame && g_bOn;
}

bool PortFrontScoreAfterLeave(void)
{
  return g_bSkirmishGame && g_bOn && PortPresentGpu();
}

bool PortFrontScoreToScreen(void)
{
  return g_bFromScore && !g_bExitNext && (g_bSkirmishGame || g_bReplayGame);
}

// The score screen, in 1.17's score dialog's place (do_score_dlg): every player of
// the game, with a skirmish's map, game type and teams named as its set-up named them. The
// game comes out of black as a screen does after a game, and OK goes on as 1.17's does.
int PortFrontScore(const ScoreEnd &end)
{
  if (!g_bOn || !PortPresentGpu())
    return -1;
  ScoreGame g = ScoreRead(end);
  if (g_bSkirmishGame) {
    std::string title, type;
    std::vector<std::string> teams;
    SkirmishPlayed(&title, &type, &teams);
    if (!title.empty())
      g.map = title;
    g.type = type;
    for (ScoreTeam &t : g.teams)
      if (t.nIndex >= 0 && t.nIndex < static_cast<int>(teams.size()))
        t.name = teams[t.nIndex];
  }
  int nWindowW = 0;
  int nWindowH = 0;
  float fRefresh = 0.0f;
  PortPresentFrontBegin(&nWindowW, &nWindowH, &fRefresh);
  g_nScreen = SCREEN_SCORE;
  g_bScoreFromHome = false;
  g_nChoice = 0;
  g_bMenuOpen = false;
  g_bMods = false;
  g_bSettings = false;
  WorldSetView("campaign", false);
  ScoreOpen(g, false);
  g_fReturn = 0.0f;
  g_bFromScore = false;
  RunScreen(nWindowW, nWindowH, fRefresh);
  ScoreClose();
  g_nScreen = SCREEN_HOME;
  // OK: back to the skirmish or the replays, through 1.17's screens
  FrontHandBack(g_nChoice == FRONT_SCORE_DONE && !g_bExitNext);
  // OK: the screen the game came from takes over as it is (PortFrontSkirmish, PortFrontReplays)
  g_bFromScore = g_nChoice == FRONT_SCORE_DONE;
  return g_nChoice == FRONT_SCORE_DONE ? 7 : 0;
}

bool PortFrontLaunchPending(void)
{
  return !g_frontCanvas.empty() || !g_frontMap.empty();
}

// Campaign, in 1.17's campaign screen's place (do_cmpgn_dlg, do_xcampaign_dlg): the glue screen
// to go on to (8, 7 or 9 a mission's briefing, 0x0D Load Game, 5 the profile screen, 0 home),
// or -1 when the front end is off and 1.17's screen runs.
bool PortFrontCinematicWatched(void)
{
  return CampaignWatchedTake();
}

int PortFrontCampaign(void)
{
  // a seat of a LAN room where the front end cannot draw (a browser's, W3): after a match it
  // watched, home, where it waits for the room's next word (SeatWait), not 1.17's campaign screen
  if (!PortPresentGpu() && PortCommandOptions()->szJoin[0] && PortLanWatchAfterGame())
    return 0;
  if (!g_bOn || !PortPresentGpu())
    return -1;
  const int nAfter = AfterGame();
  // Exit Game on the score screen: home, which exits
  if (g_bExitNext)
    return 0;
  // a trainer session's game, ended: home, where the trainer's entry opens on its sessions
  if (PortTrainerAfterGame())
    return 0;
  // a LAN match watched: home, where the room opens again
  if (PortLanWatchAfterGame())
    return 0;
  // a game of the AI lab's farm watched: home, where the lab opens again
  if (PortLabWatchAfterGame())
    return 0;
  // a map the live channel switches to (map_push, map_load): home at once, where it starts,
  // with nothing drawn on the way (the game's last frames stay black, port_motion.cpp)
  if (PortFrontLaunchPending())
    return 0;
  if (nAfter >= 0)
    return nAfter;
  int nWindowW = 0;
  int nWindowH = 0;
  float fRefresh = 0.0f;
  PortPresentFrontBegin(&nWindowW, &nWindowH, &fRefresh);
  g_nScreen = SCREEN_CAMPAIGN;
  g_nChoice = 0;
  g_bMenuOpen = false;
  g_bMods = false;
  g_bSettings = false;
  WorldSetView("campaign", false);
  CampaignOpen();
  RunScreen(nWindowW, nWindowH, fRefresh);
  if (g_nChoice == FRONT_CAMPAIGN_DONE && CampaignWatching())
    CampaignWatchStart();
  CampaignClose();
  // the save Load Game opened starts now the screen is black
  if (g_nChoice == FRONT_LOAD_STARTED)
    PortLaunchStart();
  if (g_nChoice == FRONT_LOAD_STARTED || (g_nChoice == FRONT_CAMPAIGN_DONE && CampaignGluesMode() >= 7 &&
                                          CampaignGluesMode() <= 9))
    ProfileCountGame();
  g_nScreen = SCREEN_HOME;
  FrontHandBack(g_nChoice == FRONT_CAMPAIGN_DONE && CampaignGluesMode() == 0 && !g_bExitNext);
  if (g_nChoice == 2)  // the window's Close, or WM_QUIT
    g_bExitNext = true;
  return g_nChoice == FRONT_CAMPAIGN_DONE ? CampaignGluesMode() : 0;
}

namespace FrontKit {

void FrontLog(const char *pszFormat, ...)
{
  static const PortLogger s_log("hyperion_front.log");
  va_list args;
  va_start(args, pszFormat);
  char sz[512];
  SDL_vsnprintf(sz, sizeof sz, pszFormat, args);
  va_end(args);
  s_log.Line(sz);
}

}  // namespace FrontKit

void PortFrontLaunchNext(const char *pszCanvas, const char *pszMap, bool bUms)
{
  g_frontCanvas = pszCanvas != NULL ? pszCanvas : "";
  g_frontMap = pszMap != NULL ? pszMap : "";
  g_bFrontMapUms = bUms;
}

int PortFrontStartMode(void)
{
  // a LAN match watched, its replay as it is so far (port_lanfeed.cpp)
  if (LanWatchReady())
    return LanWatchLaunch();
  // the live channel's: a switch waiting on it ends when it cannot start
  if (!g_frontMap.empty()) {
    const std::string path = g_frontMap;
    g_frontMap.clear();
    const int nStarted = PortLaunchMapFile(path.c_str(), g_bFrontMapUms);
    if (!nStarted)
      PortControlLaunchFailed(("the map could not be started (for an .hxm, hyperion_hxm.log says why): " + path).c_str());
    return nStarted;
  }
  if (!g_frontCanvas.empty()) {
    const std::string tileset = g_frontCanvas;
    g_frontCanvas.clear();
    const int nStarted = PortLaunchCanvas(tileset.c_str());
    if (!nStarted)
      PortControlLaunchFailed(("the map could not be made (" + tileset + "); hyperion_canvas.log says why").c_str());
    return nStarted;
  }
  const PortMod &m = ActiveMod();
  if (m.mode == "canvas")
    return PortLaunchCanvas(m.tileset.c_str());
  PortBenchSettings bench;
  if (m.mode == "bench" && BenchTake(&bench))
    return PortBenchFromMenus(bench) ? TRUE : FALSE;
  TrainerSetup trainer;
  if (m.mode == "trainer" && TrainerTake(&trainer))
    return PortTrainerFromMenus(trainer) ? TRUE : FALSE;
  if (m.mode == "lab")
    return PortLabWatchLaunch();  // a game of the farm watched
  return FALSE;
}
