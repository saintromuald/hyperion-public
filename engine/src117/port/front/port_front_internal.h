// port_front_internal.h -- what the front end's main files share (port_front.cpp, and its
// _home, _settings and _mods parts): the screens' state, Settings' rows, and the functions one
// file calls in another. Only those files include it.
#ifndef PORT_FRONT_INTERNAL_H
#define PORT_FRONT_INTERNAL_H

#include <mutex>
#include <string>
#include <SDL3/SDL.h>
#include "port_frontkit.h"
#include "../data/port_mods.h"      // PortMod
#include "../render/port_present.h"   // PortDisplayPick

namespace Front {

using namespace FrontKit;

// The screen the loop runs: home in do_main_dlg's place, Skirmish in Play Custom's, Campaign
// in 1.17's campaign screen's, Score in its score screen's.
enum { SCREEN_HOME, SCREEN_SKIRMISH, SCREEN_CAMPAIGN, SCREEN_REPLAYS, SCREEN_BENCH, SCREEN_TRAINER, SCREEN_LAN,
       SCREEN_LAB, SCREEN_SCORE };

// The nav bar's entries and the Menu's: an id, the label, the key.
struct NavEntry {
  const char *pszId;
  const char *pszLabel;
  char cKey;
};

const NavEntry kNav[] = {{"menu", PT_("Home"), 'H'},         {"campaign", PT_("Campaign"), 'C'},
                         {"skirmish", PT_("Skirmish"), 'S'}, {"multiplayer", PT_("Multiplayer"), 'M'},
                         {"replays", PT_("Replays"), 'R'},   {"mods", PT_("Mods"), 'D'}};

// The Menu: Remastered's main-menu entries in its order (remastered-exe-menus.md), then
// Hyperion's Patch Notes (port_front_notes.cpp) and Exit. Settings is Hyperion's name for
// Remastered's Options.
const NavEntry kDrop[] = {{"settings", PT_("Settings"), 'O'}, {"collections", PT_("Collections"), 'C'},
                          {"hotkeys", PT_("Hotkeys"), 'H'},   {"credits", PT_("Show Credits"), 'R'},
                          {"intro", PT_("View Intro"), 'I'},  {"notes", PT_("Patch Notes"), 'P'},
                          {"exit", PT_("Exit Game"), 'X'}};

// Settings' tabs.
struct Tab {
  const char *pszId;
  const char *pszLabel;
  char cKey;
};

const Tab kTabs[] = {{"game", PT_("Game"), 'G'},          {"graphics", PT_("Graphics"), 'P'},
                     {"sound", PT_("Sound"), 'S'},        {"interface", PT_("Interface"), 'I'},
                     {"hotkeys", PT_("Hotkeys"), 'H'},    {"access", PT_("Accessibility"), 'A'},
                     {"hyperion", PT_("Data and mods"), 'D'}, {"performance", PT_("Performance"), 'F'},
                     {"hud", PT_("HUD"), 'U'}};

const int kTabCount = static_cast<int>(sizeof kTabs / sizeof kTabs[0]);

// The loop's state (port_front.cpp).
extern int g_nChoice;  // what the screen chose: 1.17's main-menu result, 0 while it runs
extern int g_nMod;  // PortMods, the active one
extern bool g_bMods;  // the Mods screen is open
extern int g_nModPick;  // the mod its page shows
extern bool g_bMenuOpen;
extern bool g_bNewsOpen;
extern int g_nScreen;
extern float g_fFrameDt;  // the frame's seconds, for what eases
extern int g_nMenuQuality;

// Settings' state (port_front_settings.cpp).
extern bool g_bSettings;  // the dialog is open
extern int g_nTab;  // kTabs
extern int g_nTip;  // the ? under the pointer
// What Confirm asks over the screens (port_front_settings.cpp): Restore Defaults, a restart for
// a setting, whether to keep a new display, a restart for the active mod's data
// (ModsRestartText), or whether a tool mod may open the live channel (ModsChannelText).
enum { CONFIRM_NONE, CONFIRM_RESTORE, CONFIRM_RESTART, CONFIRM_DISPLAY, CONFIRM_MOD_RESTART, CONFIRM_MOD_CHANNEL };
extern int g_nConfirm;

// The loop and the nav (port_front.cpp).
const PortMod &ActiveMod(void);
const char *ScreenId(void);
bool Enabled(const char *pszId);
void CardToggle(void);
void Choose(const char *pszId);
void NavPick(const char *pszId);
int MenuQuality(void);
int ScDialog(const char *pszTitle, const char *pszBody, const char *const *ppszButtons, const char *pszKeys, int nButtons);
extern unsigned g_nFrameNo;
bool FrontOn(void);
// The kit's frame outside the menus' loop too (the game menu over a game): a window message into
// the frame's input, true when taken; the frame's start before its keys and layout, its end after.
bool FrontInputMsg(void *hWnd, unsigned int uMsg, unsigned long long wParam, long long lParam);
void FrameBegin(void);
void FrameEnd(void);

// The home screen (port_front_home.cpp).
void NavBar(void);
void Profile(void);
void Taskbar(void);
// SC2's Menu panel, the home screen's and a game's: an entry's label, key and whether it can be
// chosen; ScMenu gives the entry clicked, MENU_BACK for the step back or a click on the
// blackout, or MENU_NONE.
struct MenuItem {
  const char *pszLabel;
  char cKey;
  bool bEnabled;
};
enum { MENU_NONE = -1, MENU_BACK = -2 };
int ScMenu(const char *pszTitle, const MenuItem *pItems, int nItems, const char *pszBack, char cBack);
// ScMenu's frame, fWidth units wide and fHeight high (0: as its content), left open for the
// content, which Close ends: true when the blackout round it was clicked.
bool ScPanel(const char *pszTitle, float fWidth, float fHeight);
void MenuPanel(void);
void Hero(void);
void Plate(void);
void News(void);

// Settings (port_front_settings.cpp). Over a game (the game menu's Options) it opens on
// pszTab when one is named, greys what a game cannot take, and its Accept waits for
// SettingsGameStep, which the game menu's step calls out of the frame.
void SettingsOpen(bool bInGame = false, const char *pszTab = NULL);
void SettingsClose(void);
std::string ElideFront(const char *psz, float fMax);
void Confirm(void);
void SettingsDialog(void);
void SettingsKeys(void);
void SettingsGameStep(void);
bool SettingsInGame(void);

// Settings' HUD tab (port_front_hud.cpp): its picks read as Settings opens, written
// on Accept and set back by Restore Defaults; the tab itself, with Classic's rows (the "hud"
// rows of kSettings) drawn by pfnRow.
void HudTabOpen(void);
void HudTabAccept(void);
void HudTabRestore(void);
void HudTab(const std::vector<int> &rows, void (*pfnRow)(int));
// Mods (port_front_mods.cpp).
void ModsOpen(void);
void ModsDialog(void);
void ModsKeys(void);
// The restart Activate offers: what changes once Hyperion starts again (CONFIRM_MOD_RESTART).
std::string ModsRestartText(void);
// What a tool mod's channel lets in, asked once for each such mod (CONFIRM_MOD_CHANNEL), and the
// answer: allowed or not, the restart its data needs asked next.
std::string ModsChannelText(void);
void ModsChannelAnswered(bool bAllow);

}  // namespace Front

#endif
