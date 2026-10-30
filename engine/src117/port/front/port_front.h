// port_front.h -- the front end as the game reaches it: the home screen in do_main_dlg's place,
// the window's input while it runs, and the games it starts (port_front.cpp; docs/modules/front-
// end.md).
#ifndef PORT_FRONT_H
#define PORT_FRONT_H

#include "../../h/platform.h"  // LONG, ULONG and the pointer-sized types
#include <string>

// The game the front end starts next from its main menu, as soon as it is up: the canvas (its
// tileset; port_canvas.h's PortCanvasPushNext gives it a pushed map) or a map file
// (port_front.cpp, for the live channel's map_push and map_load).
void PortFrontLaunchNext(const char *pszCanvas, const char *pszMap, bool bUms);
// Whether one is waiting to start: the menus' music is not started for it (snd.cpp).
bool PortFrontLaunchPending(void);

// The start of a skirmish (port_intro.cpp): the game held before its first frame
// while the map comes out of black and the countdown plays. run_game calls
// PortIntroGameStarts as a game begins, which arms it for a game the skirmish screen started
// (PortFrontSkirmishGame), and PortIntroArmed is then true until it ends. RunGameLoop asks
// PortIntroHolds each pass, true while no frame may run. It is a layer over the game
// (port_overlay.h), which draws it and holds the player's keys and buttons while it holds.
bool PortFrontSkirmishGame(void);

// Hyperion's front end (port_front.cpp). PortFrontMainMenu runs the home screen in
// do_main_dlg's place and returns 1.17's main-menu result (3 single player, 4 multiplayer,
// 2 exit, 8 the intro, 9 the credits, 0xFFF0 when the window closed), *pbExpansion the
// active mod's, or -1 when the front end is off and 1.17's menu runs. PORT_FRONT_MODE is the
// active mod's own mode, which PortFrontStartMode starts, TRUE when a game is set to
// start; PORT_FRONT_LAN a LAN room's match, its chat room played by port_lanlobby.cpp.
enum { PORT_FRONT_MODE = 0x100, PORT_FRONT_LAN = 0x101 };
int PortFrontMainMenu(int *pbExpansion);
int PortFrontStartMode(void);
// While the front end runs, WndProc hands PortFrontWndMsg the window's mouse and keys, true
// when taken.
int PortFrontActive(void);
// The screen it shows, while PortFrontActive: "menu" (home), "skirmish", "campaign", "replays",
// "multiplayer", "score", or a mod's own ("bench", "trainer", "lab"); for Discord's line
// (port_discord.cpp).
const char *PortFrontScreen(void);
int PortFrontWndMsg(void *hWnd, unsigned int uMsg, UINT_PTR wParam, LONG_PTR lParam, LONG_PTR *plResult);
// The screen a choice goes to after the profile screen ("skirmish", "replays"), once, or NULL.
const char *PortFrontAfterProfile(void);
// PortFrontPassProfile is TRUE, once after Campaign, Skirmish or Replays on the home screen,
// when the profile chosen last still has its character file, which it makes the local
// player's: do_login_dlg goes on as after its OK. PortFrontProfileChosen keeps the profile
// chosen on the profile screen.
int PortFrontPassProfile(void);
void PortFrontProfileChosen(const char *pszName);
// PortFrontSkirmish runs Hyperion's skirmish set-up in Play Custom's place (do_create_dlg,
// single player), starting at speed nSpeed: 0x0C when it made the game, 0 to go home, -1
// when the front end is off.
int PortFrontSkirmish(int nSpeed);
// The game speed the skirmish screen last started a game at, 0 to 6.
int PortFrontSkirmishSpeed(void);
// PortFrontCampaign runs Hyperion's campaign screen in 1.17's place (do_cmpgn_dlg,
// do_xcampaign_dlg): the glue screen to go on to, or -1 when the front end is off.
int PortFrontCampaign(void);
// Whether the cinematic just played was the campaign screen's Watch Again, once: the game goes
// back to that screen rather than on to the mission after it.
bool PortFrontCinematicWatched(void);
// PortFrontScore runs Hyperion's score screen in 1.17's score dialog's place
// (do_score_dlg), with what 1.17 worked out at the game's end: 7, 1.17's OK, or -1 when the front
// end is off and the dialog runs; anything else when the window closed.
// PortFrontScoreAfterLeave: Quit Mission in a skirmish Hyperion's menus set up goes on to the
// score screen as a defeat (opt_dlgs.cpp), where 1.17 goes back to its menus.
struct ScoreEnd;
int PortFrontScore(const ScoreEnd &end);
bool PortFrontScoreAfterLeave(void);
// PortFrontScoreToScreen: the score screen's OK goes back to the skirmish or replays screen the
// game came from, which takes over from it as it is, with the menus' music (do_score_dlg).
bool PortFrontScoreToScreen(void);
// PortFrontReplays runs Hyperion's replays screen in 1.17's replay list's place
// (do_create_dlg's replay mode): 0x0C when a replay is set to play, 0 to go home, -1 when the
// front end is off.
int PortFrontReplays(void);

// Settings as the live channel reads and sets them: every row, as JSON members; one setting's
// value and the word it shows (a choice's or steps' word, a toggle's on or off, a slider's per
// cent), empty when there is no such row; and one set by its label to fValue (a toggle 0 or 1,
// a slider 0 to 1, steps and choices their index) and, for a slider with a check box, nOn 0 or
// 1 (-1 leaves it). False with the reason in *pAnswer; else *pAnswer is the row as it now is
// and whether it waits on a restart.
std::string PortSettingsLiveJson(void);
std::string PortSettingLiveWord(const char *pszName, float *pf);
bool PortSettingLiveSet(const std::string &name, double fValue, int nOn, std::string *pAnswer);

#endif
