// port_front_gamemenu.cpp -- the menu a game opens over itself, in Hyperion's style.
// 1.17's game menu (F10, Alt+M, the console's and a HUD's Menu button) and the dialogs it leads
// to, End Mission and its confirms, Help, Keystroke Help, Tips and Mission Objectives, are drawn by
// the front end's kit over the game: SC2's Menu panel (ScMenu) as the main menu has it, and its
// confirm (ScDialog), in the colours of Settings' theme. Options is the main menu's Settings, in
// place of 1.17's Options, Speed, Sound, Video and Network dialogs, with what a game cannot take
// greyed (port_front_settings.cpp); Save Game and Load Game are the menus' Load Game dialog
// (port_front_load.cpp). The dialogs the game raises over itself are the confirm's look too:
// Victory and Defeat, and the console's Alliances and Messaging.
//
// modeless_dlg_startup asks PortGameMenuFor before it loads one of those dialogs. The menu then
// holds the state 1.17's dialog would (modeless_dlg_enter: the game paused in single player, the
// accelerators the sound's alone) and lets go of it as 1.17's does; a dialog of 1.17's opening over
// it, or the game's end, puts it away (PortGameMenuDrop).
//
// A layer over the game (port_overlay.h) draws the panels and takes the mouse and the keys while
// the menu is up. What a panel chose waits for PortGameMenuStep, in the game's loop, so no game
// call is made while the frame is drawn. Presentation only: every choice is made as 1.17's handler
// makes it (PortGameMenuDo, opt_dlgs.cpp).

#include <windows.h>
#include <stdlib.h>
#include <string>
#include <vector>
#include <SDL3/SDL.h>
#include "port_gamemenu.h"
#include "port_front_internal.h"
#include "port_frontscreens.h"        // FrontLog
#include "../platform/port_winmsg.h"  // WinMsgWindowHandle, WinMsgKeyState, WinMsgReleaseCapture
#include "../render/port_maptext.h"   // MapTextUtf8
#include "../render/port_overlay.h"
#include "../render/port_present.h"   // PortPresentGpu, PortPresentRedraw, PortCursorRecheck
#include "../gameplay/port_scoregame.h"  // ScoreColour
#include "../gameplay/port_watchgame.h"  // PortWatchResult
#include "../../gen/globals117.h"     // gwGameMode

namespace Front {
namespace {

// The panels, each 1.17's dialog of the same name: the menu, End Mission, its three confirms, Help,
// Keystroke Help, Tips and Mission Objectives; Settings, Save Game and Load Game; Victory and
// Defeat; Alliances and Messaging.
enum { GP_MENU, GP_END, GP_RESTART, GP_QUIT, GP_EXIT, GP_HELP, GP_KEYS, GP_TIPS, GP_OBJECTIVES, GP_SETTINGS,
       GP_SAVE, GP_LOAD, GP_VICTORY, GP_DEFEAT, GP_ALLIES, GP_MESSAGES };

// 1.17's dialogs that are a panel here, by the files modeless_dlg_startup is given, the names
// HYPERION_GAME_MENU_OPEN takes, and for an option dialog the tab of Settings it opens on.
struct Template {
  const char *pszFile;
  const char *pszName;
  int nPanel;
  const char *pszTab;
};

const Template kTemplates[] = {
    {"rez\\gamemenu.bin", "menu", GP_MENU, NULL},         {"rez\\abrtmenu.bin", "end", GP_END, NULL},
    {"rez\\restart.bin", "restart", GP_RESTART, NULL},    {"rez\\quit2mnu.bin", "quit", GP_QUIT, NULL},
    {"rez\\quitrepl.bin", "quit", GP_QUIT, NULL},         {"rez\\quit.bin", "exit", GP_EXIT, NULL},
    {"rez\\helpmenu.bin", "help", GP_HELP, NULL},         {"rez\\help.bin", "keys", GP_KEYS, NULL},
    {"rez\\tips_dlg.bin", "tips", GP_TIPS, NULL},         {"rez\\objctdlg.bin", "objectives", GP_OBJECTIVES, NULL},
    {"rez\\options.bin", "options", GP_SETTINGS, NULL},   {"rez\\spd_dlg.bin", "speed", GP_SETTINGS, "game"},
    {"rez\\snd_dlg.bin", "sound", GP_SETTINGS, "sound"},  {"rez\\video.bin", "video", GP_SETTINGS, "graphics"},
    {"rez\\netdlg.bin", "network", GP_SETTINGS, "game"},  {"rez\\savegame.bin", "save", GP_SAVE, NULL},
    {"rez\\loadgame.bin", "load", GP_LOAD, NULL},       {"rez\\wmission.bin", "victory", GP_VICTORY, NULL},
    {"rez\\lmission.bin", "defeat", GP_DEFEAT, NULL},    {"rez\\allyfltr.bin", "allies", GP_ALLIES, NULL},
    {"rez\\teamfltr.bin", "allies", GP_ALLIES, NULL},    {"rez\\msgfltr.bin", "messages", GP_MESSAGES, NULL}};

// The tab Settings opens on when its panel opens next, from the dialog asked for.
const char *g_pszTab;

// The panels up, the top last; none while the menu is down.
std::vector<int> g_stack;

// What a panel chose, for PortGameMenuStep: a panel opened over it, the top one closed (Previous,
// Cancel), the whole menu closed, a step of 1.17's (GM_DO_*), another tip, or Show Tips at Startup
// switched; Victory's or Defeat's choice (1 Continue Playing); a box of Alliances' switched (a
// player's Allied or Shared Vision, or Allied Victory), and Alliances' or Messaging's Accept. The
// first a pass asks for is the one made.
enum { ASK_NONE, ASK_PUSH, ASK_POP, ASK_CLOSE, ASK_DO, ASK_TIP, ASK_TIP_START, ASK_END, ASK_ALLY, ASK_VISION,
       ASK_ALLIED_VICTORY, ASK_ALLIES_ACCEPT, ASK_MESSAGES_ACCEPT };
int g_nAsk = ASK_NONE;
int g_nAskWhat;

void Ask(int nAsk, int nWhat = 0)
{
  if (g_nAsk == ASK_NONE) {
    g_nAsk = nAsk;
    g_nAskWhat = nWhat;
  }
}

// What this game's menu shows, read each pass, and the panels' texts in UTF-8, read as their panel
// opens: the objectives, the keystroke help's lines (a key and what it does, or a heading), the tip.
PortGameMenuGame g_game;
std::string g_objectives;
struct HelpLine {
  std::string keys, what;
};
std::vector<HelpLine> g_help;
std::string g_tip;
bool g_bTipStart;

// Victory's or Defeat's text (GM_END_*), whether Continue Playing shows, and when it opened: a
// defeat ends the game itself after 20 seconds, as loss_activate's timer does.
int g_nEnd;
bool g_bEndContinue;
Uint64 g_nEndAt;

// Alliances' rows and Messaging's list, their names in UTF-8, and Messaging's choice till Accept.
PortAllies g_allies;
struct AllyNames {
  std::string name;
  std::vector<std::string> members;
};
std::vector<AllyNames> g_allyNames;
PortMessages g_messages;
std::vector<std::string> g_messageNames;
int g_nMessageChoice;

// A text of the game's in UTF-8, its line breaks kept and its colour codes left out.
std::string GameText(const char *psz)
{
  std::string s, line;
  for (const char *p = psz; p != NULL; ++p) {
    if (*p == '\0' || *p == '\n') {
      s += MapTextUtf8(line.c_str());
      line.clear();
      if (*p == '\0')
        break;
      s += '\n';
    } else if (*p != '\r') {
      line += *p;
    }
  }
  return s;
}

void ReadHelp(void)
{
  g_help.clear();
  const int n = PortHelpOpen();
  for (int i = 1; i <= n; ++i) {
    const std::string s = MapTextUtf8(PortHelpLine(i));
    HelpLine l;
    const size_t at = s.find(" - ");
    if (at == std::string::npos) {
      l.what = s;
    } else {
      l.keys = s.substr(0, at);
      l.what = s.substr(at + 3);
    }
    if (SDL_strcasecmp(l.what.c_str(), "Exit Starcraft") == 0)
      l.what = PortText("Exit Program");  // the menu's own name for it (Remastered's spelling "StarCraft")
    g_help.push_back(l);
  }
  while (!g_help.empty() && g_help.back().keys.empty() && g_help.back().what.empty())
    g_help.pop_back();
}

// A panel's texts, read as it opens; the tips' from the tip do_tips_dlg would open on when the game
// asked for them, else the one after the last shown.
void Read(int nPanel, bool bFromGame)
{
  switch (nPanel) {
    case GP_OBJECTIVES:
      g_objectives = GameText(PortObjectivesText());
      break;
    case GP_KEYS:
      ReadHelp();
      break;
    case GP_TIPS:
      PortTipsOpen(bFromGame && PortTipsStartup());
      g_tip = GameText(PortTipsText());
      g_bTipStart = PortTipsAtStart();
      break;
    case GP_SETTINGS:
      SettingsOpen(true, g_pszTab);
      g_pszTab = NULL;
      break;
    case GP_SAVE:
    case GP_LOAD:
      GameSavesOpen(nPanel == GP_SAVE);
      break;
    case GP_VICTORY:
    case GP_DEFEAT:
      g_nEnd = PortGameEndKind(nPanel == GP_VICTORY, &g_bEndContinue);
      PortGameEndSound(nPanel == GP_VICTORY);  // as 1.17's dialogs open
      FrontLog("game menu: %s, text %d", nPanel == GP_VICTORY ? "victory" : "defeat", g_nEnd);
      g_nEndAt = SDL_GetTicks();
      break;
    case GP_ALLIES:
      PortAlliesOpen(&g_allies);
      g_allyNames.assign(g_allies.nRows, AllyNames());
      for (int i = 0; i < g_allies.nRows; ++i) {
        g_allyNames[i].name = MapTextUtf8(g_allies.aRows[i].pszName);
        for (int k = 0; k < g_allies.aRows[i].nMembers; ++k)
          g_allyNames[i].members.push_back(MapTextUtf8(g_allies.aRows[i].apszMembers[k]));
      }
      break;
    case GP_MESSAGES:
      PortMessagesOpen(&g_messages);
      g_messageNames.clear();
      for (int i = 0; i < g_messages.nNames; ++i)
        g_messageNames.push_back(MapTextUtf8(g_messages.apszNames[i]));
      g_nMessageChoice = g_messages.nChoice;
      break;
  }
}

void Unread(int nPanel)
{
  if (nPanel == GP_KEYS) {
    PortHelpClose();
  } else if (nPanel == GP_TIPS) {
    PortTipsClose();
  } else if (nPanel == GP_SETTINGS) {
    if (g_bSettings)
      SettingsClose();
    g_nConfirm = CONFIRM_NONE;
  } else if (nPanel == GP_SAVE || nPanel == GP_LOAD) {
    GameSavesClose();
  } else if (nPanel == GP_VICTORY || nPanel == GP_DEFEAT) {
    PortGameEndClosed();
  }
}

// A panel that has closed itself (Settings' Accept or Cancel, and the word after it; Save's or
// Load's Cancel), which the menu then takes off.
bool Closed(int nPanel)
{
  if (nPanel == GP_SETTINGS)
    return !g_bSettings && g_nConfirm == CONFIRM_NONE;
  if (nPanel == GP_SAVE || nPanel == GP_LOAD)
    return !LoadGameUp();
  return false;
}

// The kit's input let go of, as the menu comes and goes: the front end's loop is not up in a game,
// so it is the menu's alone.
void InputReset(void)
{
  if (g_bDown)
    WinMsgReleaseCapture();
  g_bDown = g_bPressed = g_bReleased = false;
  g_nPressedOn = 0;
  g_keys.clear();
  g_typed.clear();
  g_nWheel = 0;
}

bool Available(void)
{
  return gwGameMode == 3 && FrontOn() && PortPresentGpu() && !PortPresentRemote();
}

// The menu up at nPanel, alone: Previous then closes it, as 1.17's dialog opened by its own key
// closes. Entered unless it is up already.
void Enter(int nPanel, bool bFromGame)
{
  if (g_stack.empty()) {
    PortGameMenuDo(GM_DO_ENTER);
    ThemeApply();
    InputReset();
    PortGameMenuRead(&g_game);
  } else {
    while (!g_stack.empty()) {
      Unread(g_stack.back());
      g_stack.pop_back();
    }
  }
  g_stack.push_back(nPanel);
  g_nAsk = ASK_NONE;
  Read(nPanel, bFromGame);
  PortCursorRecheck();
  PortPresentRedraw();
}

// ---------------------------------------------------------------------------
// The panels.

// An entry of a panel of them: its label and key, whether it shows and can be chosen, and what
// it asks for.
struct Entry {
  const char *pszLabel;
  char cKey;
  bool bShown, bEnabled;
  int nAsk, nWhat;
};

// ScMenu's panel of entries: a click or an entry's key asks for what it does; Escape, the step
// back or a click round the panel goes back a panel.
void Entries(const char *pszTitle, const Entry *pEntries, int nEntries, const char *pszBack)
{
  MenuItem items[10];
  const Entry *apShown[10];
  int n = 0;
  for (int i = 0; i < nEntries && n < 10; ++i)
    if (pEntries[i].bShown) {
      items[n] = MenuItem{pEntries[i].pszLabel, pEntries[i].cKey, pEntries[i].bEnabled};
      apShown[n++] = &pEntries[i];
    }
  int nChosen = ScMenu(pszTitle, items, n, pszBack, 0);
  for (int i = 0; i < n && nChosen == MENU_NONE; ++i)
    if (items[i].bEnabled && Key(items[i].cKey))
      nChosen = i;
  if (nChosen == MENU_NONE && Key(VK_ESCAPE))
    nChosen = MENU_BACK;
  if (nChosen == MENU_BACK)
    Ask(ASK_POP);
  else if (nChosen >= 0)
    Ask(apShown[nChosen]->nAsk, apShown[nChosen]->nWhat);
}

// End Mission's Quit, captioned as abortmenu_update_timer captions it.
const char *const kQuitLabels[] = {PT_("Quit Mission"), PT_("Surrender"), PT_("Victory"), PT_("Quit (Draw)"),
                                   PT_("Exit Replay")};
const char kQuitKeys[] = {'Q', 'S', 'V', 'Q', 'E'};

void GameMenuPanel(void)
{
  const Entry kEntries[] = {
      {PortText("Save Game"), 'S', g_game.bSave, !g_game.bSaveOff, ASK_PUSH, GP_SAVE},
      {PortText("Load Game"), 'L', g_game.bLoad, true, ASK_PUSH, GP_LOAD},
      {PortText("Pause Game"), 'P', g_game.bPause, true, ASK_DO, GM_DO_PAUSE},
      {PortText("Resume Game"), 'R', g_game.bResume, true, ASK_DO, GM_DO_RESUME},
      {PortText("Options"), 'O', true, true, ASK_PUSH, GP_SETTINGS},
      {PortText("Help"), 'H', true, true, ASK_PUSH, GP_HELP},
      {PortText("Mission Objectives"), 'J', true, true, ASK_PUSH, GP_OBJECTIVES},
      {PortText("End Mission"), 'E', true, true, ASK_PUSH, GP_END}};
  Entries(PortText("Game Menu"), kEntries, static_cast<int>(sizeof kEntries / sizeof kEntries[0]),
          PortText("Return to Game"));
}

void EndPanel(void)
{
  const Entry kEntries[] = {
      {PortText("Restart Mission"), 'R', g_game.bRestart, true, ASK_PUSH, GP_RESTART},
      {PortText(kQuitLabels[g_game.nQuit]), kQuitKeys[g_game.nQuit], true, true, ASK_PUSH, GP_QUIT},
      {PortText("Exit Program"), 'X', g_game.bExit, true, ASK_PUSH, GP_EXIT}};
  Entries(PortText("End Mission"), kEntries, static_cast<int>(sizeof kEntries / sizeof kEntries[0]),
          PortText("Previous"));
}

void HelpPanel(void)
{
  const Entry kEntries[] = {{PortText("Keystroke Help"), 'H', true, true, ASK_PUSH, GP_KEYS},
                            {PortText("Tips"), 'T', true, true, ASK_PUSH, GP_TIPS}};
  Entries(PortText("Help"), kEntries, static_cast<int>(sizeof kEntries / sizeof kEntries[0]), PortText("Previous"));
}

// A confirm, the main menu's: yes makes nDo, Cancel or Escape goes back a panel.
void SurePanel(const char *pszTitle, const char *pszBody, const char *pszYes, char cYes, int nDo)
{
  const char *const apszButtons[] = {pszYes, PortText("Cancel")};
  const char aszKeys[] = {cYes, ' ', '\0'};
  const int n = ScDialog(pszTitle, pszBody, apszButtons, aszKeys, 2);
  if (n == 0)
    Ask(ASK_DO, nDo);
  else if (n == 1 || Key(VK_ESCAPE))
    Ask(ASK_POP);
}

// The row of a text panel's buttons, centred under what it shows.
void ButtonRow(void)
{
  Decl row;
  row.d.layout.sizing.width = Grow();
  row.d.layout.childGap = Gap(Fs(14));
  row.d.layout.childAlignment.x = CLAY_ALIGN_X_CENTER;
  row.d.layout.padding = Pad(0, 0, Fs(16), 0);
  Open(row);
}

bool RowButton(const char *pszId, const char *pszLabel, char cKey, bool bGold)
{
  Decl b;
  b.d.id = Id(pszId);
  b.d.layout.sizing.width = Fixed(Sc(240));
  b.d.layout.sizing.height = Fixed(Sc(62));
  return ScButton(b, pszLabel, cKey, bGold);
}

// A text panel's text, its line breaks kept.
void BodyText(const std::string &s, float fMinHeight)
{
  Decl body;
  body.d.layout.sizing.width = Grow();
  body.d.layout.sizing.height = Fit();
  body.d.layout.sizing.height.size.minMax.min = Sc(fMinHeight);
  body.d.layout.childAlignment.y = CLAY_ALIGN_Y_CENTER;
  Open(body);
  Text(Keep(s), UI_FACE_TEXT, Fs(18), 0.0f, kScInkMsg, -1, 1.4f);
  Close();
}

void ObjectivesPanel(void)
{
  bool bBack = ScPanel(PortText("Mission Objectives"), 900, 0);
  BodyText(g_objectives, 160);
  ButtonRow();
  bBack |= RowButton("gmback", PortText("Previous"), 0, false);
  Close();
  Close();
  if (bBack || Key(VK_ESCAPE) || Key(VK_RETURN))
    Ask(ASK_POP);
}

// One line of the keystroke help: the keys and what they do, a heading, or a gap.
void HelpRow(const HelpLine &l)
{
  Decl row;
  row.d.layout.sizing.width = Grow();
  row.d.layout.sizing.height = Fixed(Sc(l.keys.empty() && l.what.empty() ? 12 : 27));
  row.d.layout.childAlignment.y = CLAY_ALIGN_Y_CENTER;
  Open(row);
  if (l.keys.empty()) {
    Text(Keep(l.what), UI_FACE_BOLD, Fs(16), 0.0f, kScInkHead);
  } else {
    Decl keys;
    keys.d.layout.sizing.width = Fixed(Sc(200));
    Open(keys);
    Text(Keep(l.keys), UI_FACE_BOLD, Fs(16), 0.0f, kScInkHi);
    Close();
    Text(Keep(l.what), UI_FACE_TEXT, Fs(16), 0.0f, kScInkMsg);
  }
  Close();
}

// The keystroke help in two columns, split at the gap nearest the list's middle.
void KeysPanel(void)
{
  int nSplit = static_cast<int>(g_help.size());
  for (int i = 0; i < static_cast<int>(g_help.size()); ++i)
    if (g_help[i].keys.empty() && g_help[i].what.empty() &&
        abs(i * 2 - static_cast<int>(g_help.size())) < abs(nSplit * 2 - static_cast<int>(g_help.size())))
      nSplit = i;
  bool bBack = ScPanel(PortText("Keystroke Help"), 1180, 0);
  if (g_help.empty())
    BodyText(PortText("The game's files this copy of Hyperion reads have no list of keys."), 120);
  Decl cols;
  cols.d.layout.sizing.width = Grow();
  cols.d.layout.childGap = Gap(Fs(40));
  Open(cols);
  for (int c = 0; c < 2; ++c) {
    Decl col;
    col.d.layout.layoutDirection = CLAY_TOP_TO_BOTTOM;
    col.d.layout.sizing.width = Grow();
    Open(col);
    const int nFrom = c == 0 ? 0 : nSplit + 1;
    const int nTo = c == 0 ? nSplit : static_cast<int>(g_help.size());
    for (int i = nFrom; i < nTo; ++i)
      HelpRow(g_help[i]);
    Close();
  }
  Close();
  ButtonRow();
  bBack |= RowButton("gmback", PortText("Previous"), 0, false);
  Close();
  Close();
  if (bBack || Key(VK_ESCAPE) || Key(VK_RETURN))
    Ask(ASK_POP);
}

void TipsPanel(void)
{
  bool bBack = ScPanel(PortText("Tips"), 900, 0);
  BodyText(g_tip, 200);
  Decl check;
  check.d.id = Id("gmtipstart");
  if (ScCheck(check, PortText("Show Tips at Startup"), 'S', g_bTipStart) || Key('S'))
    Ask(ASK_TIP_START);
  ButtonRow();
  if (RowButton("gmtipprev", PortText("Previous Tip"), 'P', false) || Key('P'))
    Ask(ASK_TIP, -1);
  if (RowButton("gmtipnext", PortText("Next Tip"), 'N', false) || Key('N'))
    Ask(ASK_TIP, 1);
  bBack |= RowButton("gmback", PortText("OK"), 'O', true);
  Close();
  Close();
  if (bBack || Key('O') || Key(VK_ESCAPE) || Key(VK_RETURN))
    Ask(ASK_POP);
}

// Victory or Defeat, the confirm's look: 1.17's text and its OK, as victory_activate labels it,
// and Continue Playing in a live multiplayer game's victory. Escape and F10 do nothing, as the
// game's keys are the sound's alone under 1.17's.
void ResultPanel(void)
{
  struct Result {
    const char *pszTitle, *pszBody, *pszOk;
    char cOk;
  };
  static const Result kResults[] = {
      {PT_("Victory"), PT_("Congratulations! You are victorious!"), PT_("Victory"), 'V'},
      {PT_("Victory"), PT_("Congratulations! You are victorious!"), PT_("End Mission"), 'E'},
      {PT_("Draw"), PT_("The game ended in a draw."), PT_("OK"), 'O'},
      {PT_("Disconnected"), PT_("You were disconnected."), PT_("OK"), 'O'},
      {PT_("Replay"), PT_("The replay has finished."), PT_("Exit Replay"), 'E'},
      {PT_("Defeat"), PT_("You failed to achieve victory!"), PT_("OK"), 'O'},
      {PT_("Game Over"), "", PT_("Leave Game"), 'L'}};
  const Result &r = kResults[g_nEnd];
  // a watched skirmish's end: who won, and Keep Watching in Continue Playing's place
  const bool bWatched = g_nEnd == GM_END_WATCHED;
  const char *const apszButtons[] = {PortText(r.pszOk),
                                     bWatched ? PortText("Keep Watching") : PortText("Continue Playing")};
  const char aszKeys[] = {r.cOk, bWatched ? 'K' : 'C', '\0'};
  const int n = ScDialog(PortText(r.pszTitle), bWatched ? Keep(GameText(PortWatchResult())) : PortText(r.pszBody),
                         apszButtons, aszKeys, g_bEndContinue ? 2 : 1);
  if (n == 0 || (n < 0 && Key(VK_RETURN)))
    Ask(ASK_END, 0);
  else if (n == 1)
    Ask(ASK_END, 1);
}

// A check box in a column of Alliances' rows, centred in it; true when clicked while it can be.
bool BoxCell(const char *pszId, int n, float fWidth, bool bShown, bool bOn, bool bEnabled)
{
  Decl cell;
  cell.d.layout.sizing.width = Fixed(Sc(fWidth));
  cell.d.layout.sizing.height = Fixed(Sc(40));
  cell.d.layout.childAlignment.x = CLAY_ALIGN_X_CENTER;
  cell.d.layout.childAlignment.y = CLAY_ALIGN_Y_CENTER;
  Open(cell);
  bool bClicked = false;
  if (bShown) {
    const Clay_ElementId id = Id(pszId, n);
    const bool bHot = bEnabled && Over(id);
    Decl box;
    box.d.id = id;
    box.d.layout.sizing.width = Fixed(Sc(30));
    box.d.layout.sizing.height = Fixed(Sc(30));
    box.d.custom.customData = Custom(DrawScCheck, kScButton, 0.0f, (bOn ? 1 : 0) | (bHot ? 2 : 0) | (bEnabled ? 0 : 8));
    Open(box);
    Close();
    bClicked = bEnabled && Clicked(id);
  }
  Close();
  return bClicked;
}

// A column's head over Alliances' boxes.
void HeadCell(const char *pszHead, float fWidth)
{
  Decl cell;
  cell.d.layout.sizing.width = Fixed(Sc(fWidth));
  cell.d.layout.childAlignment.x = CLAY_ALIGN_X_CENTER;
  Open(cell);
  Text(pszHead, UI_FACE_BOLD, Fs(16), 0.0f, kScInkHead);
  Close();
}

const float kAllyColumn = 150.0f, kVisionColumn = 170.0f;

// Alliances: a row a player, or a team over its players' names, its colour and its boxes; then
// Allied Victory, which a replay has as Reveal Entire Map with vision alone, as 1.17's dialog does.
void AlliesPanel(void)
{
  const bool bReplay = g_allies.bReplay;
  bool bCancel = ScPanel(bReplay ? PortText("Replay Players") : PortText("Alliances"), 760, 0);
  Decl head;
  head.d.layout.sizing.width = Grow();
  head.d.layout.padding = Pad(0, 0, 0, Fs(6));
  Open(head);
  Spacer();
  if (!bReplay)
    HeadCell(PortText("Allied"), kAllyColumn);
  HeadCell(bReplay ? PortText("Vision") : PortText("Shared Vision"), kVisionColumn);
  Close();
  if (g_allies.nRows == 0)
    BodyText(bReplay ? PortText("The replay has no players to show.")
                     : PortText("There is no one else in this game to ally with."),
             60);
  for (int i = 0; i < g_allies.nRows; ++i) {
    const PortAlliesRow &r = g_allies.aRows[i];
    Decl row;
    row.d.layout.sizing.width = Grow();
    Open(row);
    Decl who;
    who.d.layout.layoutDirection = CLAY_TOP_TO_BOTTOM;
    who.d.layout.sizing.width = Grow();
    Open(who);
    Decl line;
    line.d.layout.sizing.height = Fixed(Sc(40));
    line.d.layout.childGap = Gap(Fs(12));
    line.d.layout.childAlignment.y = CLAY_ALIGN_Y_CENTER;
    Open(line);
    Decl swatch;
    swatch.d.layout.sizing.width = Fixed(Sc(18));
    swatch.d.layout.sizing.height = Fixed(Sc(18));
    swatch.d.backgroundColor = Col(UiRgb(ScoreColour(r.nNation)));
    Open(swatch);
    Close();
    Text(Keep(g_allyNames[i].name), UI_FACE_BOLD, Fs(18), 0.0f, kScInkHi);
    Close();
    for (const std::string &m : g_allyNames[i].members) {
      Decl member;
      member.d.layout.padding = Pad(Fs(30), 0, 0, Fs(4));
      Open(member);
      Text(Keep(m), UI_FACE_TEXT, Fs(16), 0.0f, kScInkMsg);
      Close();
    }
    Close();
    if (!bReplay && BoxCell("gmally", r.nNation, kAllyColumn, r.bAlly, PortAlliesIsAlly(r.nNation), !r.bAllyOff))
      Ask(ASK_ALLY, r.nNation);
    if (BoxCell("gmvision", r.nNation, kVisionColumn, r.bVision, PortAlliesHasVision(r.nNation), !r.bVisionOff))
      Ask(ASK_VISION, r.nNation);
    Close();
  }
  Decl check;
  check.d.id = Id("gmalliedvictory");
  check.d.layout.padding = Pad(0, 0, Fs(12), 0);
  if (ScCheck(check, bReplay ? PortText("Reveal Entire Map") : PortText("Allied Victory"), 'V', PortAlliesVictory(),
              !g_allies.bVictoryOff) ||
      (!g_allies.bVictoryOff && Key('V')))
    Ask(ASK_ALLIED_VICTORY);
  ButtonRow();
  const bool bAccept = RowButton("gmaccept", PortText("Accept"), 'A', true);
  bCancel |= RowButton("gmcancel", PortText("Cancel"), 'C', false);
  Close();
  Close();
  if (bAccept || Key('A') || Key(VK_RETURN))
    Ask(ASK_ALLIES_ACCEPT);
  else if (bCancel || Key('C') || Key(VK_ESCAPE))
    Ask(ASK_POP);
}

// A round choice of a column of them; true when clicked while it can be.
bool RadioLine(const char *pszId, int n, const std::string &label, bool bOn, bool bEnabled)
{
  const Clay_ElementId id = Id(pszId, n);
  const bool bHot = bEnabled && Over(id);
  Decl r;
  r.d.id = id;
  r.d.layout.sizing.height = Fixed(Sc(40));
  r.d.layout.childGap = Gap(Fs(12));
  r.d.layout.childAlignment.y = CLAY_ALIGN_Y_CENTER;
  Open(r);
  Decl dot;
  dot.d.layout.sizing.width = Fixed(Sc(28));
  dot.d.layout.sizing.height = Fixed(Sc(28));
  dot.d.custom.customData = Custom(DrawScCheck, kScButton, 0.0f, (bOn ? 1 : 0) | (bHot ? 2 : 0) | 4 | (bEnabled ? 0 : 8));
  Open(dot);
  Close();
  Text(Keep(label), UI_FACE_BOLD, Fs(18), 0.0f, !bEnabled ? kScInkOff : bOn || bHot ? kScInkHi : kScInkButton);
  Close();
  return bEnabled && Clicked(id);
}

// Messaging: whom the chat goes to, a player (a team in a team game), the allies or everyone.
void MessagesPanel(void)
{
  bool bCancel = ScPanel(PortText("Messaging"), 640, 0);
  Decl head;
  head.d.layout.padding = Pad(0, 0, 0, Fs(6));
  Open(head);
  Text(PortText("Send messages to"), UI_FACE_BOLD, Fs(16), 0.0f, kScInkHead);
  Close();
  Decl list;
  list.d.layout.layoutDirection = CLAY_TOP_TO_BOTTOM;
  list.d.layout.sizing.width = Grow();
  Open(list);
  for (int i = 0; i < static_cast<int>(g_messageNames.size()); ++i)
    if (RadioLine("gmmsg", i, g_messageNames[i], g_nMessageChoice == i, true))
      g_nMessageChoice = i;
  if (RadioLine("gmmsg", 8, PortText("Allies"), g_nMessageChoice == PORT_MSG_ALLIES, !g_messages.bAlliesOff))
    g_nMessageChoice = PORT_MSG_ALLIES;
  if (RadioLine("gmmsg", 9, PortText("Everyone"), g_nMessageChoice == PORT_MSG_EVERYONE, true))
    g_nMessageChoice = PORT_MSG_EVERYONE;
  Close();
  ButtonRow();
  const bool bAccept = RowButton("gmaccept", PortText("Accept"), 'A', true);
  bCancel |= RowButton("gmcancel", PortText("Cancel"), 'C', false);
  Close();
  Close();
  if (bAccept || Key('A') || Key(VK_RETURN))
    Ask(ASK_MESSAGES_ACCEPT, g_nMessageChoice);
  else if (bCancel || Key('C') || Key(VK_ESCAPE))
    Ask(ASK_POP);
}

// ---------------------------------------------------------------------------
// The layer over the game.

bool LayerShown(void)
{
  return !g_stack.empty();
}

void DrawPanel(int nPanel)
{
  switch (nPanel) {
    case GP_MENU:
      GameMenuPanel();
      break;
    case GP_END:
      EndPanel();
      break;
    case GP_RESTART:
      SurePanel(PortText("Restart Mission"), PortText("Are you sure you want to restart the mission?"),
                PortText("Restart Mission"), 'R', GM_DO_RESTART);
      break;
    case GP_QUIT:
      if (g_game.nQuit == GM_QUIT_REPLAY)
        SurePanel(PortText("Exit Replay"), PortText("Are you sure you want to exit the replay?"),
                  PortText("Exit Replay"), 'E', GM_DO_QUIT);
      else
        SurePanel(PortText(kQuitLabels[g_game.nQuit]), PortText("Are you sure you want to quit the mission?"),
                  PortText(kQuitLabels[g_game.nQuit]), kQuitKeys[g_game.nQuit], GM_DO_QUIT);
      break;
    case GP_EXIT:
      SurePanel(PortText("Exit Program"), PortText("Are you sure you want to exit Hyperion?"),
                PortText("Exit Program"), 'X', GM_DO_EXIT);
      break;
    case GP_HELP:
      HelpPanel();
      break;
    case GP_KEYS:
      KeysPanel();
      break;
    case GP_TIPS:
      TipsPanel();
      break;
    case GP_OBJECTIVES:
      ObjectivesPanel();
      break;
    case GP_SETTINGS:
      // its keys before its layout, as the main menu's loop has them; after Accept, the restart's
      // word alone
      if (g_bSettings)
        SettingsKeys();
      if (g_bSettings)
        SettingsDialog();
      if (g_nConfirm != CONFIRM_NONE)
        Confirm();
      break;
    case GP_SAVE:
    case GP_LOAD:
      LoadGameKeys();
      LoadGameDialog();
      break;
    case GP_VICTORY:
    case GP_DEFEAT:
      ResultPanel();
      break;
    case GP_ALLIES:
      AlliesPanel();
      break;
    case GP_MESSAGES:
      MessagesPanel();
      break;
  }
}

// Victory and Defeat stay till chosen: F10 does not put them away.
bool IsResult(int nPanel)
{
  return nPanel == GP_VICTORY || nPanel == GP_DEFEAT;
}

bool LayerDraw(int nWidth, int nHeight, float fX, float fY, float fW, float fH)
{
  (void)nWidth;
  (void)nHeight;
  (void)fX;
  (void)fY;
  (void)fW;
  (void)fH;
  if (g_stack.empty())
    return false;
  FrameBegin();
  UiLayoutBegin(g_mouseX, g_mouseY, g_bDown);
  LayoutCheckBegin();
  if (Key(VK_F10) && !IsResult(g_stack.back()))
    Ask(ASK_CLOSE);
  DrawPanel(g_stack.back());
  // a panel that closed itself goes; the one under it shows this frame already, the input spent
  if (Closed(g_stack.back())) {
    Ask(ASK_POP);
    if (g_stack.size() > 1) {
      g_keys.clear();
      g_typed.clear();
      g_bPressed = g_bReleased = false;
      g_nWheel = 0;
      DrawPanel(g_stack[g_stack.size() - 2]);
    }
  }
  LayoutCheckEnd("gamemenu");
  UiLayoutEnd();
  FrameEnd();
  return true;
}

// While the menu is up it has the mouse and the keys, as 1.17's dialog does: the game sees none.
bool LayerMouse(unsigned uMsg, unsigned long long wParam, long long lParam)
{
  if (g_stack.empty())
    return false;
  FrontInputMsg(WinMsgWindowHandle(), uMsg, wParam, lParam);
  PortPresentRedraw();
  return true;
}

// The keys going down, once each; the characters they make are the menu's too. The modifiers go
// on to the game, which has no use for them alone (and Ctrl+Alt lets the cursor go), as do the
// sound's own keys, Ctrl+M and Ctrl+S, which 1.17's dialog leaves to the accelerators, and Alt+F4.
bool LayerKey(unsigned vk, bool bDown, bool bRepeat)
{
  if (g_stack.empty())
    return false;
  // a character a key made, for Save Game's name; with Alt it is the system's
  if (!bDown) {
    if (vk >= 0x20 && vk < 0x7F && WinMsgKeyState(VK_MENU) >= 0) {
      g_typed.push_back(static_cast<char>(vk));
      PortPresentRedraw();
    }
    return true;
  }
  if (vk == VK_SHIFT || vk == VK_CONTROL || vk == VK_MENU)
    return false;
  if (WinMsgKeyState(VK_CONTROL) < 0 && (vk == 'M' || vk == 'S'))
    return false;
  if (vk == VK_F4 && WinMsgKeyState(VK_MENU) < 0)
    return false;
  // a held key counts once, but Backspace repeats in Save Game's name, as in the menus' edit boxes
  if (!bRepeat || vk == VK_BACK) {
    g_keys.push_back(static_cast<int>(vk));
    PortPresentRedraw();
  }
  return true;
}

bool LayerOutside(float fX, float fY)
{
  (void)fX;
  (void)fY;
  return !g_stack.empty();
}

bool LayerWheel(int nDelta)
{
  if (g_stack.empty())
    return false;
  g_nWheel += nDelta;
  PortPresentRedraw();
  return true;
}

const PortOverlayLayer kLayer = {"gamemenu",
                                 PORT_OVERLAY_GAMEMENU,
                                 PORT_OVERLAY_FREE_CURSOR | PORT_OVERLAY_SYSKEYS | PORT_OVERLAY_HIDES_CURSOR,
                                 LayerShown,
                                 NULL,
                                 LayerDraw,
                                 NULL,
                                 LayerMouse,
                                 LayerKey,
                                 LayerOutside,
                                 LayerWheel};
PORT_OVERLAY_LAYER(kLayer);

// HYPERION_GAME_MENU_OPEN=<panel>, a name of kTemplates': the panel opens three seconds into a
// game, for a check or a capture of it. Victory's and Defeat's choices then end the game as the
// game's own would.
void OpenAsked(void)
{
  static const char *s_pszAsked = SDL_getenv("HYPERION_GAME_MENU_OPEN");
  static Uint64 s_nAt;
  static bool s_bDone;
  if (s_pszAsked == NULL || s_bDone || gwGameMode != 3) {
    s_nAt = 0;
    return;
  }
  if (s_nAt == 0)
    s_nAt = SDL_GetTicks() + 3000;
  if (SDL_GetTicks() < s_nAt)
    return;
  s_bDone = true;
  for (const Template &t : kTemplates)
    if (SDL_strcasecmp(t.pszName, s_pszAsked) == 0) {
      if (Available()) {
        FrontLog("game menu: %s opened (HYPERION_GAME_MENU_OPEN)", t.pszName);
        g_pszTab = t.pszTab;
        Enter(t.nPanel, false);
      }
      return;
    }
  FrontLog("game menu: HYPERION_GAME_MENU_OPEN=%s names no panel", s_pszAsked);
}

bool SameGame(const PortGameMenuGame &a, const PortGameMenuGame &b)
{
  return a.bSave == b.bSave && a.bSaveOff == b.bSaveOff && a.bLoad == b.bLoad && a.bPause == b.bPause &&
         a.bResume == b.bResume && a.bRestart == b.bRestart && a.bExit == b.bExit && a.nQuit == b.nQuit;
}

}  // namespace
}  // namespace Front

using namespace Front;

bool PortGameMenuFor(const char *pszTemplate)
{
  if (pszTemplate == NULL || !Available())
    return false;
  for (const Template &t : kTemplates)
    if (SDL_strcasecmp(t.pszFile, pszTemplate) == 0) {
      g_pszTab = t.pszTab;
      Enter(t.nPanel, true);
      return true;
    }
  return false;
}

bool PortGameMenuDrop(void)
{
  if (g_stack.empty())
    return false;
  while (!g_stack.empty()) {
    Unread(g_stack.back());
    g_stack.pop_back();
  }
  g_nAsk = ASK_NONE;
  InputReset();
  PortCursorRecheck();
  PortPresentRedraw();
  return true;
}

void PortGameMenuStep(void)
{
  OpenAsked();
  if (g_stack.empty())
    return;
  PortGameMenuGame now;
  PortGameMenuRead(&now);
  if (!SameGame(now, g_game)) {
    g_game = now;
    PortPresentRedraw();
  }
  // what Settings, Save Game or Load Game chose, made out of the frame; a save or a load gone out
  // closes the menu, as 1.17's dialog closes it
  if (g_stack.back() == GP_SETTINGS)
    SettingsGameStep();
  if ((g_stack.back() == GP_SAVE || g_stack.back() == GP_LOAD) && GameSavesStep()) {
    PortGameMenuDo(GM_DO_CLOSE);
    return;
  }
  // a defeat left 20 seconds ends the game, as loss_dlg_fcn's timer does
  if (g_stack.back() == GP_DEFEAT && SDL_GetTicks() - g_nEndAt >= 20000) {
    PortGameEndTimeout();
    PortGameMenuDo(GM_DO_CLOSE);
    return;
  }
  // the name box's caret blinks, though a paused game draws no new frame for it
  static Uint64 s_nBlink;
  if (g_stack.back() == GP_SAVE && SDL_GetTicks() / 530 != s_nBlink) {
    s_nBlink = SDL_GetTicks() / 530;
    PortPresentRedraw();
  }
  const int nAsk = g_nAsk;
  const int nWhat = g_nAskWhat;
  g_nAsk = ASK_NONE;
  switch (nAsk) {
    case ASK_PUSH:
      g_stack.push_back(nWhat);
      Read(nWhat, false);
      PortPresentRedraw();
      break;
    case ASK_POP:
      if (g_stack.size() == 1) {
        PortGameMenuDo(GM_DO_CLOSE);
      } else {
        Unread(g_stack.back());
        g_stack.pop_back();
        PortPresentRedraw();
      }
      break;
    case ASK_CLOSE:
      PortGameMenuDo(GM_DO_CLOSE);
      break;
    case ASK_DO:
      PortGameMenuDo(nWhat);
      PortPresentRedraw();
      break;
    case ASK_TIP:
      PortTipsStep(nWhat);
      g_tip = GameText(PortTipsText());
      PortPresentRedraw();
      break;
    case ASK_TIP_START:
      PortTipsAtStartToggle();
      g_bTipStart = PortTipsAtStart();
      PortPresentRedraw();
      break;
    case ASK_END:
      FrontLog("game menu: %s, %s", g_stack.back() == GP_VICTORY ? "victory" : "defeat", nWhat == 1 ? "Continue Playing" : "OK");
      PortGameEndChoose(g_stack.back() == GP_VICTORY, nWhat == 1);
      PortGameMenuDo(GM_DO_CLOSE);
      break;
    case ASK_ALLY:
      PortAlliesToggleAlly(nWhat);
      PortPresentRedraw();
      break;
    case ASK_VISION:
      PortAlliesToggleVision(nWhat);
      PortPresentRedraw();
      break;
    case ASK_ALLIED_VICTORY:
      PortAlliesToggleVictory();
      PortPresentRedraw();
      break;
    case ASK_ALLIES_ACCEPT:
    case ASK_MESSAGES_ACCEPT:
      if (nAsk == ASK_ALLIES_ACCEPT)
        PortAlliesAccept();
      else
        PortMessagesAccept(nWhat);
      // Accept closes the dialog as Cancel does
      if (g_stack.size() == 1) {
        PortGameMenuDo(GM_DO_CLOSE);
      } else {
        Unread(g_stack.back());
        g_stack.pop_back();
        PortPresentRedraw();
      }
      break;
  }
}
