// port_gamemenu.h -- the menu a game opens over itself, in Hyperion's style: 1.17's game
// menu and the dialogs it leads to, drawn by the front end's kit over the game
// (port_front_gamemenu.cpp), and what the game's code gives it (lang/opt_dlgs.cpp, modeless.cpp,
// help.cpp, tips.cpp). Presentation only: every choice is made as 1.17's handler makes it.
#ifndef PORT_GAMEMENU_H
#define PORT_GAMEMENU_H

// modeless_dlg_startup asks first: true when the dialog pszTemplate names (rez\gamemenu.bin, or
// one of the game menu's others) is Hyperion's now, which its menus on, the GPU presenter up and a
// game in this window make so; 1.17's is then not loaded.
bool PortGameMenuFor(const char *pszTemplate);
// Hyperion's menu put away without a choice, by a dialog of 1.17's opening over it or by the game
// ending (modeless_dlg_enter, modeless_dlg_shutdown): true when it was up, and the caller then lets
// go of the accelerators and the cursor as for one of 1.17's.
bool PortGameMenuDrop(void);
// Once a pass of RunGameLoop, outside the frame: what the menu chose, carried out.
void PortGameMenuStep(void);

// What 1.17's game menu and End Mission would show in this game, by their own rules
// (gamemenu_activate, abortmenu_activate, abortmenu_update_timer; opt_dlgs.cpp).
struct PortGameMenuGame {
  bool bSave, bSaveOff, bLoad, bPause, bResume;  // the menu's: shown, and Save greyed
  bool bRestart, bExit;                          // End Mission's
  int nQuit;                                     // End Mission's Quit, GM_QUIT_* below
};
enum { GM_QUIT_MISSION, GM_QUIT_SURRENDER, GM_QUIT_VICTORY, GM_QUIT_DRAW, GM_QUIT_REPLAY };
void PortGameMenuRead(PortGameMenuGame *p);

// A step of the menu's, made as 1.17's handlers make it (opt_dlgs.cpp): the state of a dialog over
// the game entered and left (modeless_dlg_enter, modeless_dlg_shutdown), a pause asked for or
// ended, and End Mission's three.
enum { GM_DO_ENTER, GM_DO_CLOSE, GM_DO_PAUSE, GM_DO_RESUME, GM_DO_RESTART, GM_DO_QUIT, GM_DO_EXIT };
void PortGameMenuDo(int nAction);

// Save Game and Load Game (save_dlg.cpp), as 1.17's dialogs make them: a save by the name given,
// its trailing spaces left off, unless one of that name is there (PORT_SAVE_THERE, then again
// with bReplace), the profile has its most, 128, or the name cannot be a file's; and the load of
// the save of that name, false when 1.17's list of saves does not have it.
enum { PORT_SAVE_DONE, PORT_SAVE_THERE, PORT_SAVE_FULL, PORT_SAVE_BAD_NAME, PORT_SAVE_NOT_REPLACED };
int PortGameSave(const char *pszName, bool bReplace);
bool PortGameLoad(const char *pszName);

// Victory and defeat (VictDlg.cpp), as victory_activate and loss_activate show them: which text,
// GM_END_* below, and whether Continue Playing shows (a live multiplayer game's victory); the
// choice, OK or Continue Playing, made by victory_dlg_cmd or loss_dlg_cmd, the defeat's 20
// seconds run out as loss_dlg_fcn's timer ends it, and the dialog's close, after which the game
// may raise it again. A watched skirmish's end (port_watchgame.cpp) is Hyperion's own kind: who
// won, Leave Game as Exit Replay leaves, and Keep Watching.
enum { GM_END_VICTORY, GM_END_VICTORY_MULTI, GM_END_DRAW, GM_END_DISCONNECTED, GM_END_REPLAY, GM_END_DEFEAT,
       GM_END_WATCHED };
int PortGameEndKind(bool bVictory, bool *pbContinue);
// The sound victory_activate or loss_activate plays as its dialog opens.
void PortGameEndSound(bool bVictory);
void PortGameEndChoose(bool bVictory, bool bContinue);
void PortGameEndTimeout(void);
void PortGameEndClosed(void);

// Alliances (filters.cpp), as do_ally_filter_dlg stages them and sub_442320 or, in a team game,
// sub_442490 lays out their rows, eight at most: a player, or a team and its players' names, its
// Allied and Shared Vision boxes shown and greyed by 1.17's rules. A replay has vision alone, and
// Reveal Entire Map for Allied Victory. The boxes change the staged copy as 1.17's check boxes do;
// Accept sends it as teamfilter_dlg_cmd does.
struct PortAlliesRow {
  int nNation;
  const char *pszName;  // the player's, or the team's
  const char *apszMembers[8];  // a team's players
  int nMembers;
  bool bAlly, bAllyOff, bVision, bVisionOff;  // shown, and greyed
};
struct PortAllies {
  PortAlliesRow aRows[8];
  int nRows;
  bool bReplay, bVictoryOff;
};
void PortAlliesOpen(PortAllies *p);
bool PortAlliesIsAlly(int nNation);
bool PortAlliesHasVision(int nNation);
bool PortAlliesVictory(void);
void PortAlliesToggleAlly(int nNation);
void PortAlliesToggleVision(int nNation);
void PortAlliesToggleVictory(void);
void PortAlliesAccept(void);
// A replay's vision of one of the lobby's players turned on or off, as View Players' box for them
// and its OK turn it, Reveal Entire Map off (the HUD's players' board). What View Players
// has staged is kept.
void PortReplayVisionToggle(int nNation);

// Messaging (filters.cpp), whom the chat goes to, as do_msg_filter_dlg stages it and sub_4429A0
// lists the players, or the teams in a team game: one of the list's rows, the allies or everyone.
// Accept sets it as msgfilter_dlg_cmd does.
enum { PORT_MSG_ALLIES = -1, PORT_MSG_EVERYONE = -2 };
struct PortMessages {
  const char *apszNames[8];
  int nNames;
  int nChoice;  // a row, or PORT_MSG_*
  bool bAlliesOff;
};
void PortMessagesOpen(PortMessages *p);
void PortMessagesAccept(int nChoice);

// The texts, in 1.17's codes and Windows-1252: the objectives (opt_dlgs.cpp); the keystroke
// help's lines, 1 to the number Open gives (help.cpp); and the tips, from the tip Open chooses,
// as tips_dlg_init chooses it (tips.cpp). Each table is held from its Open to its Close.
const char *PortObjectivesText(void);
int PortHelpOpen(void);
const char *PortHelpLine(int n);
void PortHelpClose(void);
bool PortTipsStartup(void);  // what do_tips_dlg asked: the tips at a game's start
void PortTipsOpen(bool bStartup);
const char *PortTipsText(void);
void PortTipsStep(int nDelta);
bool PortTipsAtStart(void);  // Show Tips at Startup, the options' bit 0x100
void PortTipsAtStartToggle(void);
void PortTipsClose(void);

#endif
